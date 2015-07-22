#include "../precomp.hpp"
#include "layers_common.hpp"
#include "im2col.hpp"

namespace cv
{
namespace dnn
{
    //TODO: simultaneously convolution and bias addition for cache optimization
    class ConvolutionLayer : public Layer
    {
    protected:
        bool bias;
        int numOutput, group;
        int padH, padW;
        int kerH, kerW;
        int strideH, strideW;

        int inpH, inpW, inpCn;
        int outH, outW, outCn;
        int topH, topW, topCn; //switched between inp/out on deconv/conv
        int inpGroupCn, outGroupCn;
        int ksize;

        Mat colMat, biasOnesMat;

        inline bool is1x1() const;
        virtual void computeInpOutShape(const Blob &inpBlob);
        void im2col(Blob &inpBlob, int imNum, int cnGroup);

    public:
        ConvolutionLayer(LayerParams &params);
        void allocate(const std::vector<Blob*> &inputs, std::vector<Blob> &outputs);
        void forward(std::vector<Blob*> &inputs, std::vector<Blob> &outputs);
    };

    class DeConvolutionLayer : public ConvolutionLayer
    {
    protected:
        void computeInpOutShape(const Blob &inpBlob);
        void col2im(Mat &dstMat);

    public:
        DeConvolutionLayer(LayerParams &params) : ConvolutionLayer(params) {}
        void forward(std::vector<Blob*> &inputs, std::vector<Blob> &outputs);
    };


    REGISTER_LAYER_CLASS(Convolution, ConvolutionLayer)
    REGISTER_LAYER_CLASS(Deconvolution, DeConvolutionLayer)


    ConvolutionLayer::ConvolutionLayer(LayerParams &params)
    {
        getKernelParams(params, kerH, kerW, padH, padW, strideH, strideW);

        numOutput = params.get<int>("num_output");
        bias = params.get<bool>("bias_term", true);
        group = params.get<int>("group", 1);
        CV_Assert(numOutput % group == 0);

        CV_Assert(params.learnedBlobs.size() >= 1 && (!bias || params.learnedBlobs.size() >= 2));
        learnedParams.assign(params.learnedBlobs.begin(), params.learnedBlobs.begin() + (bias ? 2 : 1));

        const Blob &wgtBlob = learnedParams[0];
        CV_Assert(wgtBlob.dims() == 4 && wgtBlob.cols() == kerW && wgtBlob.rows() == kerH && wgtBlob.num() == numOutput);

        if (bias)
        {
            Blob &biasBlob = learnedParams[1];
            CV_Assert(biasBlob.total() == (size_t)numOutput);
        }
    }

    void ConvolutionLayer::allocate(const std::vector<Blob*> &inputs, std::vector<Blob> &outputs)
    {
        CV_Assert(inputs.size() > 0);

        const Blob &inpBlob = *inputs[0];
        CV_Assert(inpBlob.dims() == 4 && inpBlob.type() == CV_32F);
        computeInpOutShape(inpBlob);

        CV_Assert(inpCn % group == 0 && outCn % group == 0);
        CV_Assert(learnedParams[0].channels() == inpCn / group);
        CV_Assert(learnedParams[0].num() == outCn);

        outGroupCn = outCn / group;
        inpGroupCn = inpCn / group;
        ksize = inpGroupCn * kerH * kerW;

        outputs.resize(inputs.size());
        for (size_t i = 0; i < inputs.size(); i++)
        {
            CV_Assert(inputs[i]->type() == inpBlob.type());
            CV_Assert(inputs[i]->dims() == 4 && inputs[i]->channels() == inpBlob.channels());
            CV_Assert(inputs[i]->rows() == inpBlob.rows() && inputs[i]->cols() == inpBlob.cols());

            outputs[i].create(BlobShape(inputs[i]->num(), topCn, topH, topW));
        }

        if (!is1x1())
            colMat.create(ksize, outH * outW, inpBlob.type());

        if (bias)
            biasOnesMat = Mat::ones(1, topH * topW, inpBlob.type());
    }

    inline bool ConvolutionLayer::is1x1() const
    {
        return (kerH == 1 && kerW == 1);
    }

    void ConvolutionLayer::forward(std::vector<Blob*> &inputs, std::vector<Blob> &outputs)
    {
        Blob &wgtBlob = learnedParams[0];

        for (size_t ii = 0; ii < outputs.size(); ii++)
        {
            Blob &inpBlob = *inputs[ii];
            Blob &outBlob = outputs[ii];

            for (int n = 0; n < inpBlob.num(); n++)
            {
                for (int g = 0; g < group; g++)
                {
                    im2col(inpBlob, n, g);

                    Mat kerMat(outGroupCn, ksize, wgtBlob.type(), wgtBlob.ptrRaw(g*outGroupCn));
                    Mat dstMat(outGroupCn, outH*outW, outBlob.type(), outBlob.ptrRaw(n, g*outGroupCn));

                    cv::gemm(kerMat, colMat, 1, noArray(), 0, dstMat);

                    if (bias)
                    {
                        float *biasPtr = learnedParams[1].ptrf() + g*outGroupCn;
                        Mat biasMat(outGroupCn, 1, CV_32F, biasPtr);
                        cv::gemm(biasMat, biasOnesMat, 1, dstMat, 1, dstMat);
                    }
                }
            }
        }
    }

    void ConvolutionLayer::im2col(Blob &inpBlob, int imNum, int cnGroup)
    {
        uchar *srcPtr = inpBlob.ptrRaw(imNum, cnGroup*inpGroupCn);

        if (is1x1())
        {
            colMat = Mat(ksize, inpBlob.rows()*inpBlob.cols(), inpBlob.type(), srcPtr);
            return;
        }

        if (inpBlob.type() == CV_32F)
            im2col_cpu((float *)srcPtr, inpGroupCn, inpH, inpW, kerH, kerW, padH, padW, strideH, strideW, (float *)colMat.ptr());
        if (inpBlob.type() == CV_64F)
            im2col_cpu((double*)srcPtr, inpGroupCn, inpH, inpW, kerH, kerW, padH, padW, strideH, strideW, (double*)colMat.ptr());
    }

    void ConvolutionLayer::computeInpOutShape(const Blob &inpBlob)
    {
        inpH = inpBlob.rows();
        inpW = inpBlob.cols();
        inpCn = inpBlob.channels();

        outH = (inpH + 2 * padH - kerH) / strideH + 1;
        outW = (inpW + 2 * padW - kerW) / strideW + 1;
        outCn = learnedParams[0].num();

        topH = outH; topW = outW; topCn = outCn;
    }

    void DeConvolutionLayer::computeInpOutShape(const Blob &inpBlob)
    {
        outH = inpBlob.rows();
        outW = inpBlob.cols();
        outCn = inpBlob.channels();

        inpH = strideH * (outH - 1) + kerH - 2 * padH;
        inpW = strideW * (outW - 1) + kerW - 2 * padW;
        inpCn = learnedParams[0].channels();

        topH = inpH; topW = inpW; topCn = inpCn;
    }

    void DeConvolutionLayer::forward(std::vector<Blob*> &inputs, std::vector<Blob> &outputs)
    {
        Blob &wghtBlob = learnedParams[0];

        for (size_t ii = 0; ii < outputs.size(); ii++)
        {
            Blob &convBlob = *inputs[ii];
            Blob &decnBlob = outputs[ii];

            for (int n = 0; n < convBlob.num(); n++)
            {
                for (int g = 0; g < group; g++)
                {
                    Mat dstMat(inpGroupCn, inpH*inpW, decnBlob.type(), decnBlob.ptrRaw(n, g*inpGroupCn));
                    
                    if (is1x1())
                        colMat = dstMat;

                    Mat convMat(outGroupCn, outH*outW, convBlob.type(), convBlob.ptrRaw(n, g*inpGroupCn));
                    Mat wghtMat(outGroupCn, ksize, wghtBlob.type(), wghtBlob.ptrRaw(g*inpGroupCn));
                    cv::gemm(wghtMat, convMat, 1, noArray(), 0, colMat, GEMM_1_T);

                    col2im(dstMat);

                    if (bias)
                    {
                        float *biasPtr = learnedParams[1].ptrf() + g*outGroupCn;
                        Mat biasMat(outGroupCn, 1, CV_32F, biasPtr);
                        cv::gemm(biasMat, biasOnesMat, 1, dstMat, 1, dstMat);
                    }
                }
            }
        }
    }

    void DeConvolutionLayer::col2im(Mat &dstMat)
    {
        if (is1x1()) return;

        if (dstMat.type() == CV_32F)
            col2im_cpu((float*)colMat.ptr(), inpCn, inpH, inpW, kerH, kerW, padH, padW, strideH, strideW, (float*)dstMat.ptr());
        if (dstMat.type() == CV_64F)
            col2im_cpu((double*)colMat.ptr(), inpCn, inpH, inpW, kerH, kerW, padH, padW, strideH, strideW, (double*)dstMat.ptr());
    }
}
}