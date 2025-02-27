﻿// -----------------------------------------------------------------------------------------
// QSVEnc/NVEnc by rigaya
// -----------------------------------------------------------------------------------------
// The MIT License
//
// Copyright (c) 2020 rigaya
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// --------------------------------------------------------------------------------------------

#include "rgy_util.h"
#include "rgy_frame.h"
#if !CLFILTERS_AUF
#include "rgy_bitstream.h"
#endif

RGYFrameDataQP::RGYFrameDataQP() :
    m_frameType(0),
    m_qpScaleType(0),
#if !FOR_AUO && ENCODER_NVENC
    m_qpDev(),
    m_event(std::unique_ptr<cudaEvent_t, cudaevent_deleter>(nullptr, cudaevent_deleter())),
#endif //#if !FOR_AUO && ENCODER_NVENC
    m_qpHost() {
    m_dataType = RGY_FRAME_DATA_QP;
};

RGYFrameDataQP::~RGYFrameDataQP() {
#if !FOR_AUO && ENCODER_NVENC
    m_qpDev.reset();
    if (m_qpHost.ptr) {
        cudaFree(m_qpHost.ptr);
        m_qpHost.ptr = nullptr;
    }
    m_event.reset();
#endif //#if !FOR_AUO && ENCODER_NVENC
};

#pragma warning(push)
#pragma warning(disable: 4100) //warning C4100: 'timestamp': 引数は関数の本体部で 1 度も参照されません。
RGY_ERR RGYFrameDataQP::setQPTable(const int8_t *qpTable, int qpw, int qph, int qppitch, int scaleType, int frameType, int64_t timestamp) {
#if !FOR_AUO && ENCODER_NVENC
    m_qpScaleType = scaleType;
    m_frameType = frameType;
    if (m_qpHost.ptr == nullptr
        || m_qpHost.width != qpw
        || m_qpHost.height != qph) {
        m_qpHost.csp = RGY_CSP_Y8;
        m_qpHost.width = qpw;
        m_qpHost.height = qph;
        m_qpHost.flags = RGY_FRAME_FLAG_NONE;
        m_qpHost.pitch = ALIGN(m_qpHost.width, 128);
        m_qpHost.deivce_mem = false;
        m_qpHost.duration = 0;
        m_qpHost.timestamp = timestamp;
        m_qpHost.picstruct = RGY_PICSTRUCT_FRAME;
        m_qpHost.dataList.clear();
        if (m_qpHost.ptr) {
            cudaFree(m_qpHost.ptr);
            m_qpHost.ptr = nullptr;
        }
        auto cudaerr = cudaMallocHost(&m_qpHost.ptr, m_qpHost.pitch * m_qpHost.height);
        if (cudaerr != cudaSuccess) {
            return RGY_ERR_MEMORY_ALLOC;
        }
    }
    for (int y = 0; y < m_qpHost.height; y++) {
        memcpy(m_qpHost.ptr + y * m_qpHost.pitch, qpTable + y * qppitch, m_qpHost.width);
    }
#endif //#if !FOR_AUO && ENCODER_NVENC
    return RGY_ERR_NONE;
}
#pragma warning(pop)

#if !FOR_AUO && ENCODER_NVENC
RGY_ERR RGYFrameDataQP::transferToGPU(cudaStream_t stream) {
    if (!m_qpDev) {
        m_qpDev = std::make_unique<CUFrameBuf>(m_qpHost.width, m_qpHost.height, m_qpHost.csp);
    }
    if (!m_event) {
        m_event = std::unique_ptr<cudaEvent_t, cudaevent_deleter>(new cudaEvent_t(), cudaevent_deleter());
        auto cudaerr = cudaEventCreate(m_event.get());
        if (cudaerr != cudaSuccess) {
            return RGY_ERR_CUDA;
        }
    }
    auto cudaerr = copyFrameDataAsync(&m_qpDev->frame, &m_qpHost, stream);
    if (cudaerr != cudaSuccess) {
        return RGY_ERR_MEMORY_ALLOC;
    }
    cudaerr = cudaEventRecord(*m_event.get(), stream);
    if (cudaerr != cudaSuccess) {
        return RGY_ERR_CUDA;
    }
    return RGY_ERR_NONE;
}
#endif //#if !FOR_AUO && ENCODER_NVENC


RGYFrameDataMetadata::RGYFrameDataMetadata() : m_timestamp(-1), m_data() { m_dataType = RGY_FRAME_DATA_METADATA; };

RGYFrameDataMetadata::RGYFrameDataMetadata(const uint8_t *data, size_t size, int64_t timestamp) {
    m_dataType = RGY_FRAME_DATA_METADATA;
    m_timestamp = timestamp;
    m_data = make_vector(data, size);
}

RGYFrameDataMetadata::~RGYFrameDataMetadata() { m_data.clear(); }

#if !CLFILTERS_AUF
RGYFrameDataHDR10plus::RGYFrameDataHDR10plus() : RGYFrameDataMetadata() { m_dataType = RGY_FRAME_DATA_HDR10PLUS; };
RGYFrameDataHDR10plus::RGYFrameDataHDR10plus(const uint8_t *data, size_t size, int64_t timestamp) :
    RGYFrameDataMetadata(data, size, timestamp) {
    m_dataType = RGY_FRAME_DATA_HDR10PLUS;
};

RGYFrameDataHDR10plus::~RGYFrameDataHDR10plus() {}


std::vector<uint8_t> RGYFrameDataHDR10plus::gen_nal() const {
    std::vector<uint8_t> header = { 0x00, 0x00, 0x00, 0x01 };

    std::vector<uint8_t> buf;
    uint16_t u16 = 0x00;
    u16 |= (NALU_HEVC_PREFIX_SEI << 9) | 1;
    add_u16(buf, u16);
    buf.push_back(USER_DATA_REGISTERED_ITU_T_T35);
    auto datasize = m_data.size();
    for (; datasize > 0xff; datasize -= 0xff)
        buf.push_back((uint8_t)0xff);
    buf.push_back((uint8_t)datasize);
    vector_cat(buf, m_data);
    to_nal(buf);

    std::vector<uint8_t> nal_hdr10plus;
    nal_hdr10plus.reserve(128);
    vector_cat(nal_hdr10plus, header);
    vector_cat(nal_hdr10plus, buf);
    nal_hdr10plus.push_back(0x80);
    return nal_hdr10plus;
}

std::vector<uint8_t> RGYFrameDataHDR10plus::gen_obu() const {
    return gen_av1_obu_metadata(AV1_METADATA_TYPE_ITUT_T35, m_data);
}

RGYFrameDataDOVIRpu::RGYFrameDataDOVIRpu() : RGYFrameDataMetadata() { m_dataType = RGY_FRAME_DATA_DOVIRPU; };
RGYFrameDataDOVIRpu::RGYFrameDataDOVIRpu(const uint8_t *data, size_t size, int64_t timestamp) : 
    RGYFrameDataMetadata(data, size, timestamp) { m_dataType = RGY_FRAME_DATA_DOVIRPU; };

RGYFrameDataDOVIRpu::~RGYFrameDataDOVIRpu() { }

std::vector<uint8_t> RGYFrameDataDOVIRpu::gen_nal() const {
    return m_data;
}
std::vector<uint8_t> RGYFrameDataDOVIRpu::gen_obu() const {
    return gen_av1_obu_metadata(AV1_METADATA_TYPE_ITUT_T35, m_data);
}
#endif
