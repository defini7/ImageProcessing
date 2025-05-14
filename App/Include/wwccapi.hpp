// WWCCAPI - Windows WebCam Capturing API

#ifndef WWCCAPI_HPP
#define WWCCAPI_HPP

#include <string>
#include <list>

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mferror.h>

// TODO: Wrap around with some macros
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

namespace wwcc
{
    enum class VideoFormat
    {
        None,
        Rgb32,
        Rgb24,
        Yuy2,
        Nv12
    };

    namespace internal
    {
        uint8_t ClampInt32ToUint8(int nValue);

        void ConvertFromRGB32(uint8_t* pSrc, uint8_t* pDst, uint32_t x);
        void ConvertFromRGB24(uint8_t* pSrc, uint8_t* pDst, uint32_t x);
        void ConvertFromYUY2(uint8_t* pSrc, uint8_t* pDst, uint32_t x);
    }

    class Capturer
    {
    public:
        Capturer() = default;
        ~Capturer();

        // nDevice is an index of a device from the list of devices from EnumerateDevices method.
        // FPS = (float)nFpsNumerator / (float)nFpsDenominator.
        bool Init(unsigned long nDevice, uint32_t nWidth, uint32_t nHeight, uint32_t nFpsNumerator, uint32_t nFpsDenominator = 1);

        static std::list<std::wstring> EnumerateDevices();

        void DoCapture();

        uint32_t GetFrameWidth() const;
        uint32_t GetFrameHeight() const;
        uint32_t GetDeviceCount() const;
        
        VideoFormat GetVideoFormat() const;

        // pBuffer must be at least sizeof(uint32_t) * m_nDesiredWidth * m_nDesiredHeight in size
        void SetBuffer(uint32_t* pBuffer);

    private:
        bool CreateDevice(const DWORD nDevice);
        bool ConfigureImage(const uint32_t nWidth, const uint32_t nHeight);
        bool ConfigureDecoder();

    private:
        IMFSourceReader* m_pReader = nullptr;
        DWORD m_dwStreamIndex = -1;

        IMFMediaSource* m_pDevice = nullptr;
        uint32_t m_nDevices = 0;

        uint8_t* m_pFrame = nullptr;
        uint32_t* m_pOutput = nullptr;

        uint32_t m_nDesiredWidth = 0, m_nDesiredHeight = 0;
        uint32_t m_nFrameWidth = 0, m_nFrameHeight = 0;

        uint32_t m_nFrameSourceStep = 0;
        uint32_t m_nFrameSourceStride = 0;
        uint32_t m_nFrameStrideRGB32 = 0;

        VideoFormat m_nVideoFormat = VideoFormat::None;

        uint32_t m_nFpsNumerator = 0;
        uint32_t m_nFpsDenominator = 0;

        void (*m_fnConvert)(uint8_t*, uint8_t*, uint32_t) = nullptr;

    };

#ifdef WWCCAPI_IMPL
#undef WWCCAPI_IMPL

    uint8_t internal::ClampInt32ToUint8(int nValue)
    {
        if (nValue < 0) return 0;
        if (nValue > 255) return 255;
        return nValue;
    }

    void internal::ConvertFromRGB32(uint8_t* pSrc, uint8_t* pDst, uint32_t x)
    {
        pDst[x] = pSrc[0];
        pDst[x + 1] = pSrc[1];
        pDst[x + 2] = pSrc[2];
        pDst[x + 3] = pSrc[3];
    }

    void internal::ConvertFromRGB24(uint8_t* pSrc, uint8_t* pDst, uint32_t x)
    {
        pDst[x] = pSrc[0];
        pDst[x + 1] = pSrc[1];
        pDst[x + 2] = pSrc[2];
        pDst[x + 3] = 255;
    }

    void internal::ConvertFromYUY2(uint8_t* pSrc, uint8_t* pDst, uint32_t x)
    {
        auto yuv_to_rgb = [](int y, int u, int v, uint8_t* pBuffer)
            {
                int c = y - 16;
                int d = u - 128;
                int e = v - 128;

                // |R|   |1.164 0.000  1.596  |   |y-16 |
                // |G| = |1.164 -0.391 -0.813 | * |u-128|
                // |B|   |1.164 2.018  0.000  |   |v-128|

                pBuffer[0] = ClampInt32ToUint8((298 * c + 409 * e + 128) >> 8);
                pBuffer[1] = ClampInt32ToUint8((298 * c - 100 * d - 208 * e + 128) >> 8);
                pBuffer[2] = ClampInt32ToUint8((298 * c + 516 * d + 128) >> 8);
                pBuffer[3] = 255;
            };

        uint8_t y0 = pSrc[0];
        uint8_t u = pSrc[1];
        uint8_t y1 = pSrc[2];
        uint8_t v = pSrc[3];

        yuv_to_rgb(y0, u, v, pDst + x * 4);
        yuv_to_rgb(y1, u, v, pDst + x * 4 + 4);
    }

    Capturer::~Capturer()
    {
        if (!m_pFrame) delete[] m_pFrame;
        if (!m_pOutput) delete[] m_pOutput;

        if (m_pDevice)
        {
            m_pDevice->Shutdown();
            m_pDevice->Release();
        }

        MFShutdown();
        CoUninitialize();
    }

    bool Capturer::Init(unsigned long nDeviceID, uint32_t nWidth, uint32_t nHeight, uint32_t nFpsNumerator, uint32_t nFpsDenominator)
    {
        m_nFpsNumerator = nFpsNumerator;
        m_nFpsDenominator = nFpsDenominator;

        HRESULT hResult = CoInitialize(nullptr);

        if (FAILED(hResult))
            return false;

        hResult = MFStartup(MF_VERSION);

        if (FAILED(hResult))
            return false;

        if (!CreateDevice(nDeviceID))
            return false;

        if (!ConfigureImage(nWidth, nHeight))
            return false;

        if (!ConfigureDecoder())
            return false;

        return true;
    }

    bool Capturer::CreateDevice(const DWORD nDeviceID)
    {
        IMFAttributes* pConfig = nullptr;
        IMFActivate** ppDevices = nullptr;

        HRESULT hResult = MFCreateAttributes(&pConfig, 1);

        if (FAILED(hResult))
            return false;

        // Request video capture devices
        hResult = pConfig->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        if (FAILED(hResult))
            return false;

        hResult = MFEnumDeviceSources(pConfig, &ppDevices, &m_nDevices);

        if (FAILED(hResult) || m_nDevices == 0)
            return false;

        hResult = ppDevices[nDeviceID]->ActivateObject(IID_PPV_ARGS(&m_pDevice));

        if (SUCCEEDED(hResult))
        {
            for (DWORD i = 0; i < m_nDevices; i++)
                ppDevices[i]->Release();

            CoTaskMemFree(ppDevices);
        }

        m_dwStreamIndex = nDeviceID;

        return true;
    }

    std::list<std::wstring> Capturer::EnumerateDevices()
    {
        IMFAttributes* pConfig = nullptr;
        IMFActivate** ppDevices = nullptr;

        HRESULT hResult = MFCreateAttributes(&pConfig, 1);

        if (FAILED(hResult))
            return {};

        hResult = pConfig->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );

        if (FAILED(hResult))
            return {};

        uint32_t nDevices;
        hResult = MFEnumDeviceSources(pConfig, &ppDevices, &nDevices);

        if (FAILED(hResult) || nDevices == 0)
            return {};

        std::list<std::wstring> listDevices;

        for (DWORD i = 0; i < nDevices; i++)
        {
            WCHAR* sName = nullptr;

            HRESULT hResult = ppDevices[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                &sName, nullptr);

            if (SUCCEEDED(hResult) && sName)
            {
                listDevices.push_back(sName);
                CoTaskMemFree(sName);
            }
        }

        for (DWORD i = 0; i < nDevices; i++)
            ppDevices[i]->Release();

        CoTaskMemFree(ppDevices);
        return listDevices;
    }

    bool Capturer::ConfigureImage(const uint32_t nWidth, const uint32_t nHeight)
    {
        if (FAILED(MFCreateSourceReaderFromMediaSource(m_pDevice, nullptr, &m_pReader)))
            return false;

        m_nDesiredWidth = nWidth;
        m_nDesiredHeight = nHeight;

        DWORD nIndex = 0;
        uint32_t nBestError = -1; // std::numeric_limits<uint32_t>::max()

        IMFMediaType* pNativeType = nullptr;

        while (SUCCEEDED(m_pReader->GetNativeMediaType(m_dwStreamIndex, nIndex, &pNativeType)))
        {
            uint32_t nFrameWidth, nFrameHeight;
            MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &nFrameWidth, &nFrameHeight);

            // Does the desired size perfectly fits one of the webcam sizes?
            if (nWidth == nFrameWidth && nHeight == nFrameHeight)
            {
                m_nFrameWidth = nFrameWidth;
                m_nFrameHeight = nFrameHeight;
                break;
            }

            // Pick one of the available sizes that's less than the desired size
            // and then choose the closest one

            if (nWidth <= nFrameWidth && nHeight <= nFrameHeight)
            {
                int nWidthError = (int)nFrameWidth - (int)nWidth;
                int nHeightError = (int)nFrameHeight - (int)nHeight;

                if (nWidthError < nBestError && nFrameHeight < nBestError)
                {
                    m_nFrameWidth = nFrameWidth;
                    m_nFrameHeight = nFrameHeight;
                    nBestError = max(nWidthError, nHeightError);
                }
            }

            nIndex++;
        }

        if (m_nFrameWidth == 0 || m_nFrameHeight == 0)
            return false;

        m_nFrameStrideRGB32 = m_nFrameWidth * 4;

        return true;
    }

#define DIE_IF(fail) do { if (fail) { bResult = false; goto end; } } while (false)

    bool Capturer::ConfigureDecoder()
    {
        IMFMediaType* pNativeType = nullptr;

        if (FAILED(m_pReader->GetCurrentMediaType(m_dwStreamIndex, &pNativeType)))
            return false;

        bool bResult = true;
        IMFMediaType* pType = nullptr;
        GUID guid;

        // Changing the default media type
        DIE_IF(FAILED(pNativeType->GetGUID(MF_MT_MAJOR_TYPE, &guid)));
        DIE_IF(FAILED(MFCreateMediaType(&pType)));
        DIE_IF(FAILED(pType->SetGUID(MF_MT_MAJOR_TYPE, guid)));

        // Check for a video
        DIE_IF(guid != MFMediaType_Video);
        DIE_IF(FAILED(pNativeType->GetGUID(MF_MT_SUBTYPE, &guid)));
        
    #define SET(fss, vf, fnc) { m_nFrameSourceStep = fss; m_nVideoFormat = vf; m_fnConvert = fnc; }

        if (guid == MFVideoFormat_RGB32)
            SET(4, VideoFormat::Rgb32, internal::ConvertFromRGB32)
        else if (guid == MFVideoFormat_RGB24)
            SET(3, VideoFormat::Rgb24, internal::ConvertFromRGB24)
        else if (guid == MFVideoFormat_YUY2)
            SET(2, VideoFormat::Yuy2, internal::ConvertFromYUY2)
        else
        {
            // TODO: Support NV12
            bResult = false;
            goto end;
        }

    #undef set

        DIE_IF(FAILED(pType->SetGUID(MF_MT_SUBTYPE, guid)));

        // Set target fps
        DIE_IF(FAILED(MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, m_nFpsNumerator, m_nFpsDenominator)));

        // Set frame size
        DIE_IF(FAILED(MFSetAttributeSize(pType, MF_MT_FRAME_SIZE, m_nFrameWidth, m_nFrameHeight)));

        // Send everything to the reader
        DIE_IF(FAILED(m_pReader->SetCurrentMediaType(m_dwStreamIndex, nullptr, pType)));

        // Allocate memory for the raw image
        m_nFrameSourceStride = m_nFrameWidth * m_nFrameSourceStep;
        m_pFrame = new uint8_t[m_nFrameStrideRGB32 * m_nFrameHeight];

    end:
        pNativeType->Release();
        pType->Release();

        return bResult;
    }

#undef DIE_IF

#define DIE_IF(fail) do { if (fail) goto end; } while (false)

    void Capturer::DoCapture()
    {
        IMFSample* pSample = nullptr;

        while (true)
        {
            DWORD nFlags;

            while (1)
            {
                // Reading a sample in a sync mode
                HRESULT hResult = m_pReader->ReadSample(
                    m_dwStreamIndex, 0, nullptr,
                    &nFlags, nullptr, &pSample
                );

                DIE_IF(FAILED(hResult));

                // Check if the sample is ready
                if ((nFlags & MF_SOURCE_READERF_STREAMTICK) == 0 && pSample)
                    break;
            }

            DIE_IF(nFlags & MF_SOURCE_READERF_ENDOFSTREAM);

            if (nFlags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
            {
                // The format has changed
                DIE_IF(!ConfigureDecoder());
            }

            IMFMediaBuffer* pBuffer = nullptr;
            DIE_IF(!pSample || FAILED(pSample->ConvertToContiguousBuffer(&pBuffer)));

            uint8_t* pData = nullptr;

            if (FAILED(pBuffer->Lock(&pData, nullptr, nullptr)))
            {
                pBuffer->Release();
                goto end;
            }

            for (uint32_t y = 0; y < m_nFrameHeight; y++)
            {
                uint8_t* pSrcRow = pData + y * m_nFrameSourceStride;
                uint8_t* pDstRow = m_pFrame + y * m_nFrameStrideRGB32;

                for (uint32_t x = 0; x < m_nFrameWidth; x += m_nFrameSourceStep, pSrcRow += 4)
                    m_fnConvert(pSrcRow, pDstRow, x);
            }

            // Scaling down the image and storing each pixel as one uint32_t instead of four uint8_t

            uint32_t* pDst = m_pOutput;
            uint32_t* pSrc = reinterpret_cast<uint32_t*>(m_pFrame);

            for (uint32_t y = 0; y < m_nDesiredHeight; y++)
                for (uint32_t x = 0; x < m_nDesiredWidth; x++, pDst++)
                {
                    size_t i = y * m_nFrameHeight / m_nDesiredHeight * m_nFrameWidth + x * m_nFrameWidth / m_nDesiredWidth;
                    *pDst = pSrc[i];
                }

            pBuffer->Release();
            break;
        }

    end:
        if (pSample)
            pSample->Release();
    }

    uint32_t Capturer::GetFrameWidth() const { return m_nFrameWidth; }
    uint32_t Capturer::GetFrameHeight() const { return m_nFrameHeight; }
    uint32_t Capturer::GetDeviceCount() const { return m_nDevices; }

    VideoFormat Capturer::GetVideoFormat() const { return m_nVideoFormat; }

    void Capturer::SetBuffer(uint32_t* pBuffer) { m_pOutput = pBuffer; }
}

#endif

#endif