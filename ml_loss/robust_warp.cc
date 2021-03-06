// for registration
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/common_shape_fns.h"

// for the kernel
#include "tensorflow/core/framework/op_kernel.h"

#include <vector>
#include <algorithm>
#include <iostream>

using namespace tensorflow;

float computeWeights(float scorePos, std::vector<float> scoresNeg, int noClasses);

// register the operation
REGISTER_OP("RobustWarp")
  .Input("y_true: float")
  .Input("y_pred: float")
  .Output("loss: float")
  .SetShapeFn(shape_inference::ScalarShape);

// implement the kernel
class RobustWarpOp : public OpKernel {
  public:
  explicit RobustWarpOp(OpKernelConstruction* context) : OpKernel(context) {}

  void Compute(OpKernelContext* context) override {

    // get the input tensors
    const Tensor& y_true_tensor = context->input(0);
    auto y_true = y_true_tensor.matrix<float>();

    const Tensor& y_pred_tensor = context->input(1);
    auto y_pred = y_pred_tensor.matrix<float>();

    // get the input shapes
    TensorShape inputShapeTensor = y_true_tensor.shape();
    int batchSize = inputShapeTensor.dim_size(0);
    int noClasses = inputShapeTensor.dim_size(1) - 1;

    float s = -0.8;
    float beta = 0.9;

    // create the output container
    Tensor* output_tensor = NULL;
    OP_REQUIRES_OK(context, context->allocate_output(0, TensorShape({}), &output_tensor));

    // calculate the output
    float loss = 0;
    for (int indExample = 0; indExample < batchSize; indExample++)
    {
      float cu = y_true(indExample, noClasses);

      std::vector<float> scoresPos, scoresNeg;
      for (int indClasses = 0; indClasses < noClasses; indClasses++)
      {
        if (y_true(indExample, indClasses) > 0.5)
          scoresPos.push_back(y_pred(indExample, indClasses));
        else
          scoresNeg.push_back(y_pred(indExample, indClasses));
      }

      for (int indPos = 0; indPos < scoresPos.size(); indPos++)
      {
        loss += std::max((float)0, (float)(1 - scoresPos[indPos])) * cu;
        loss -= std::max((float)0, (float)(s - scoresPos[indPos])) * cu * beta;
      }
            
      for (int indNeg = 0; indNeg < scoresNeg.size(); indNeg++)
      {
        loss += std::max((float)0, (float)(1 + scoresNeg[indNeg])) * cu;
        loss -= std::max((float)0, (float)(s + scoresNeg[indNeg])) * cu * beta;
      }

      for (int indPos = 0; indPos < scoresPos.size(); indPos++)
      {
        float L = computeWeights(scoresPos[indPos], scoresNeg, noClasses);
        for (int indNeg = 0; indNeg < scoresNeg.size(); indNeg++)
        {
          float hinge1 = std::max((float)0, (float)(1 - scoresPos[indPos] + scoresNeg[indNeg]));
          float hinge2 = std::max((float)0, (float)(s - scoresPos[indPos] + scoresNeg[indNeg]));
          loss += L * cu * (hinge1 - beta * hinge2);
        }
      }
    }

    // write the output to the output tensor
    auto output = output_tensor->flat<float>();
    output(0) = loss / (batchSize * 10);
  }
};

float computeWeights(float scorePos, std::vector<float> scoresNeg, int noClasses)
{
  std::random_shuffle(scoresNeg.begin(), scoresNeg.end());

  int noTrials;
  for (noTrials = 0; noTrials < scoresNeg.size(); noTrials++)
  {
    if (1 - scorePos + scoresNeg[noTrials] > 0)
      break;
  }
	noTrials++;

  int rankPos = round((noClasses - 1) / (float)noTrials);

  float L = 0;
  for (int ind = 0; ind < rankPos; ind++)
    L += 1 / (float)(ind + 1);

  return L;
}

// register the kernel
REGISTER_KERNEL_BUILDER(Name("RobustWarp").Device(DEVICE_CPU), RobustWarpOp);