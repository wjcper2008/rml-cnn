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
REGISTER_OP("RobustWarpGrad")
  .Input("y_true: float")
  .Input("y_pred: float")
  .Output("grad: float");

// implement the kernel
class RobustWarpGradOp : public OpKernel {
  public:
  explicit RobustWarpGradOp(OpKernelConstruction* context) : OpKernel(context) {}

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
    Tensor* gradOutTensor = NULL;
    OP_REQUIRES_OK(context, context->allocate_output(0, inputShapeTensor, &gradOutTensor));
    auto grad = gradOutTensor->matrix<float>();

    // fill the output
    for (int indExample = 0; indExample < batchSize; indExample++)
    {
      float cu = y_true(indExample, noClasses);

      std::vector<float> scoresPos, scoresNeg;
      std::vector<int> labelsPos, labelsNeg;
      grad(indExample, noClasses) = 0;
      for (int indClasses = 0; indClasses < noClasses; indClasses++)
      {
        grad(indExample, indClasses) = 0;
        if (y_true(indExample, indClasses) > 0.5)
        {
          scoresPos.push_back(y_pred(indExample, indClasses));
          labelsPos.push_back(indClasses);
        }
        else
        {
          scoresNeg.push_back(y_pred(indExample, indClasses));
          labelsNeg.push_back(indClasses);
        }
      }

      for (int indPos = 0; indPos < scoresPos.size(); indPos++)
      {
        if (1 - scoresPos[indPos] > 0)
          grad(indExample, labelsPos[indPos]) -= cu / (batchSize * 10);
        if (s - scoresPos[indPos] > 0)
          grad(indExample, labelsPos[indPos]) += (cu * beta) / (batchSize * 10);
      }
        
      for (int indNeg = 0; indNeg < scoresNeg.size(); indNeg++)
      {
        if (1 + scoresNeg[indNeg] > 0)
          grad(indExample, labelsNeg[indNeg]) += cu / (batchSize * 10);
        if (s + scoresNeg[indNeg] > 0)
          grad(indExample, labelsNeg[indNeg]) -= (cu * beta) / (batchSize * 10);
      }

      for (int indPos = 0; indPos < scoresPos.size(); indPos++)
      {
        float L = computeWeights(scoresPos[indPos], scoresNeg, noClasses);
        float normL = (L * cu) / (batchSize * 10);
        for (int indNeg = 0; indNeg < scoresNeg.size(); indNeg++)
        {
          if (1  - scoresPos[indPos] + scoresNeg[indNeg] > 0)
          {
            grad(indExample, labelsPos[indPos]) -= normL;
            grad(indExample, labelsNeg[indNeg]) += normL;
          }
          if (s  - scoresPos[indPos] + scoresNeg[indNeg] > 0)
          {
            grad(indExample, labelsPos[indPos]) += normL * beta;
            grad(indExample, labelsNeg[indNeg]) -= normL * beta;
          }
        }
      }
    }
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
REGISTER_KERNEL_BUILDER(Name("RobustWarpGrad").Device(DEVICE_CPU), RobustWarpGradOp);