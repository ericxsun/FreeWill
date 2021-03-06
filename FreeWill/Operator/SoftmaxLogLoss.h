#ifndef SOFTMAXLOGLOSS_H
#define SOFTMAXLOGLOSS_H

#include "Operator.h"
#include "cublas_v2.h"
#include "cudnn.h"
#include "SoftmaxLogLoss_CUDA.h"

namespace FreeWill
{

    template<DeviceType DeviceUsed = DeviceType::CPU_NAIVE, typename DataType = float>
    class SoftmaxLogLoss : public Operator<DeviceUsed>
    {
    protected:
        using Operator<DeviceUsed>::input;
        using Operator<DeviceUsed>::output;
        using Operator<DeviceUsed>::m_deviceId;
        cudnnTensorDescriptor_t m_inputGPUTensorDescriptor;
        cudnnTensorDescriptor_t m_outputGPUTensorDescriptor;

    public:
        SoftmaxLogLoss(unsigned int deviceId = 0)
            : Operator<DeviceUsed>({"Input", "Label"},{"Cost","Output"}, deviceId)
        {
            CHECK_GPU;

            if constexpr (DeviceUsed == DeviceType::GPU_CUDA)
            {
                RUN_CUDNN(cudnnCreateTensorDescriptor(&m_inputGPUTensorDescriptor));
                RUN_CUDNN(cudnnCreateTensorDescriptor(&m_outputGPUTensorDescriptor));
            }
        }

        virtual ~SoftmaxLogLoss() override
        {
            CHECK_GPU;

            if constexpr (DeviceUsed == DeviceType::GPU_CUDA)
            {
                RUN_CUDNN(cudnnDestroyTensorDescriptor(m_inputGPUTensorDescriptor));
                RUN_CUDNN(cudnnDestroyTensorDescriptor(m_outputGPUTensorDescriptor));

                m_inputGPUTensorDescriptor = 0;
                m_outputGPUTensorDescriptor = 0;
            }
        }

        virtual bool init() override 
        {
            CHECK_GPU;

            FAIL_IF (!input("Input") || !input("Label") || !output("Cost") || !output("Output"));

            FAIL_IF (input("Input")->shape() != output("Output")->shape());

            FAIL_IF (input("Input")->shape().dimension() != 2);

            FAIL_IF (input("Label")->shape().dimension() != 2 || output("Cost")->shape().dimension() != 2);

            FAIL_IF (1 != input("Label")->shape()[0] || 1 != output("Cost")->shape()[0]);

            unsigned int batchSize = input("Input")->shape()[1];

            FAIL_IF (batchSize != input("Label")->shape()[1] || batchSize != output("Cost")->shape()[1]);


            if constexpr (DeviceUsed == DeviceType::GPU_CUDA)
            {
                cudnnDataType_t dataType = CUDNN_DATA_FLOAT;
                if constexpr (std::is_same<DataType,float>::value)
                {
                    dataType = CUDNN_DATA_FLOAT;
                }
                else if constexpr (std::is_same<DataType,double>::value)
                {
                    dataType = CUDNN_DATA_DOUBLE;
                }

                unsigned int vectorSize = input("Input")->shape()[0];
                //printf("vector size: %d, batchSize:%d\n", vectorSize, batchSize);
                int dimA[4] = {(int)batchSize,(int)vectorSize, 1, 1};
                int strideA[4] = {(int)vectorSize,1,1,1};

                RUN_CUDNN(cudnnSetTensorNdDescriptor(m_inputGPUTensorDescriptor,
                                           dataType,
                                           4,
                                           dimA,
                                           strideA));

                RUN_CUDNN(cudnnSetTensorNdDescriptor(m_outputGPUTensorDescriptor,
                                           dataType,
                                           4,
                                           dimA,
                                           strideA));
                //printf("done\n");

                /* looks like the dimA is in reverse order...
                RUN_CUDNN(cudnnSetTensor4dDescriptorEx(m_inputGPUTensorDescriptor,dataType,
                                             4,3,2,1, 6,2,1,1));

                int dimnb = 0;
                RUN_CUDNN(cudnnGetTensorNdDescriptor(m_inputGPUTensorDescriptor,
                4,
                &dataType,
                &dimnb,
                dimA,
                strideA));

                printf("debug: %d, %d,%d,%d,%d | %d,%d,%d,%d\n", dimnb, dimA[0], dimA[1], dimA[2], dimA[3],strideA[0],strideA[1],strideA[2],strideA[3]);
                */

            }

            return true;
        }

        virtual void evaluate() override
        {
            CHECK_GPU;

            Tensor<DeviceUsed, DataType> *_input = input("Input")->template toType<DataType>();
            Tensor<DeviceUsed, unsigned int> *_label = input("Label")->template toType<unsigned int>();
            Tensor<DeviceUsed, DataType> *_cost = output("Cost")->template toType<DataType>();
            Tensor<DeviceUsed, DataType> *_output = output("Output")->template toType<DataType>();

            unsigned int batchSize = _input->shape()[1];
            unsigned int vectorSize = _input->shape()[0];

            if constexpr (DeviceUsed == DeviceType::CPU_NAIVE)
            {
                for(unsigned int b = 0; b < batchSize; ++b)
                {


                    DataType maximum = (*_input)[b * vectorSize];

                    for(unsigned int i = 1;i<vectorSize;++i)
                    {
                        if ((*_input)[b*vectorSize + i] > maximum)
                        {
                            maximum = (*_input)[b*vectorSize + i];
                        }
                    }

                    DataType expSum = 0;
                    unsigned int label = (*_label)[b];

                    for(unsigned int i=0;i<vectorSize;++i)
                    {
                        DataType v = (*_input)[b*vectorSize + i] - maximum;

                        v = std::exp(v);

                        (*_output)[b*vectorSize + i] = v;

                        if (i == label)
                        {
                            (*_cost)[b] = v;
                        }

                        expSum += v;

                    }

                    for(unsigned int i=0;i<vectorSize;++i)
                    {
                        (*_output)[b*vectorSize+i] = (*_output)[b*vectorSize+i] / expSum;
                    }

                    (*_cost)[b] /= expSum;

                    (*_cost)[b] = -log((*_cost)[b]);
            
                }

            }
            else if constexpr (DeviceUsed == DeviceType::GPU_CUDA)
            {
                DataType alpha = 1;
                DataType beta = 0;
                RUN_CUDNN(cudnnSoftmaxForward(Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId), CUDNN_SOFTMAX_ACCURATE,
                            CUDNN_SOFTMAX_MODE_CHANNEL, &alpha,
                            m_inputGPUTensorDescriptor,
                            _input->gpuDataHandle(),
                            &beta,
                            m_outputGPUTensorDescriptor,
                            _output->gpuDataHandle()
                            ));

                softmaxLogLossCUDAKernel(_output->gpuDataHandle(), _label->gpuDataHandle(), _cost->gpuDataHandle(), vectorSize, batchSize);
                
            }


        }
    };
}

#endif


