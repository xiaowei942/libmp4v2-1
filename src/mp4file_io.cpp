/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is MPEG4IP.
 *
 * The Initial Developer of the Original Code is Cisco Systems Inc.
 * Portions created by Cisco Systems Inc. are
 * Copyright (C) Cisco Systems Inc. 2001.  All Rights Reserved.
 *
 * Contributor(s):
 *      Dave Mackie     dmackie@cisco.com
 */

#include "impl.h"

namespace mp4v2 {
namespace impl {

///////////////////////////////////////////////////////////////////////////////

// MP4File low level IO support

uint64_t MP4File::GetPosition(FILE* pFile)
{
    if (m_memoryBuffer == NULL) {
        if (pFile == NULL) {
            ASSERT(m_pFile);
            uint64_t fpos;
            if (m_virtual_IO->GetPosition(m_pFile, &fpos) != 0) {
                throw new MP4Error("getting position via Virtual I/O", "MP4GetPosition");
            }
            return fpos;
        } else {
#if 0
            fpos_t fpos;
            if (fgetpos(pFile, &fpos) < 0) {
                throw new MP4Error(errno, "MP4GetPosition");
            }
            uint64_t ret;
            FPOS_TO_VAR(fpos, uint64_t, ret);
            return ret;
#else
            off_t fpos = ftello(pFile);
            if (fpos == -1) {
                throw new MP4Error(errno, "MP4GetPosition");
            }
            return (uint64_t)fpos;
#endif
        }
    } else {
        return m_memoryBufferPosition;
    }
}

void MP4File::SetPosition(uint64_t pos, FILE* pFile)
{
    if (m_memoryBuffer == NULL) {
        if (pFile == NULL) {
            ASSERT(m_pFile);
            if (m_virtual_IO->SetPosition(m_pFile, pos) != 0) {
                throw new MP4Error("setting position via Virtual I/O", "MP4SetPosition");
            }
        }   else {
#if 0
            fpos_t fpos;
            VAR_TO_FPOS(fpos, pos);
            if (fsetpos(pFile, &fpos) < 0) {
                throw new MP4Error(errno, "MP4SetPosition");
            }
#else
            off_t fpos = (off_t)pos;
            if (fseeko(pFile, fpos, SEEK_SET) == -1) {
                throw new MP4Error(errno, "MP4SetPosition");
            }
#endif
        }
    } else {
        if (pos >= m_memoryBufferSize) {
            //          abort();
            throw new MP4Error("position out of range", "MP4SetPosition");
        }
        m_memoryBufferPosition = pos;
    }
}

uint64_t MP4File::GetSize()
{
    if (m_mode == 'w') {
        // we're always positioned at the end of file in write mode
        // except for short intervals in ReadSample and FinishWrite routines
        // so we rely on the faster approach of GetPosition()
        // instead of flushing to disk, and then stat'ing the file
        m_fileSize = GetPosition();
    } // else read mode, fileSize was determined at Open()

    return m_fileSize;
}

void MP4File::ReadBytes(uint8_t* pBytes, uint32_t numBytes, FILE* pFile)
{
    // handle degenerate cases
    if (numBytes == 0) {
        return;
    }

    ASSERT(pBytes);
    WARNING(m_numReadBits > 0);

    if (m_memoryBuffer == NULL) {
        if (pFile == NULL) {
            ASSERT(m_pFile);
            if (m_virtual_IO->Read(m_pFile, pBytes, numBytes) != numBytes) {
                throw new MP4Error("not enough bytes, reached end-of-file",     "MP4ReadBytes");
            }
        }   else {
            if (fread(pBytes, 1, numBytes, pFile) != numBytes) {
                if (feof(pFile)) {
                    throw new MP4Error(
                        "not enough bytes, reached end-of-file",
                        "MP4ReadBytes");
                } else {
                    throw new MP4Error(errno, "MP4ReadBytes");
                }
            }
        }
    } else {
        if (m_memoryBufferPosition + numBytes > m_memoryBufferSize) {
            throw new MP4Error(
                "not enough bytes, reached end-of-memory",
                "MP4ReadBytes");
        }
        memcpy(pBytes, &m_memoryBuffer[m_memoryBufferPosition], numBytes);
        m_memoryBufferPosition += numBytes;
    }
    return;
}

void MP4File::PeekBytes(uint8_t* pBytes, uint32_t numBytes, FILE* pFile)
{
    uint64_t pos = GetPosition(pFile);
    ReadBytes(pBytes, numBytes, pFile);
    SetPosition(pos, pFile);
}

void MP4File::EnableMemoryBuffer(uint8_t* pBytes, uint64_t numBytes)
{
    ASSERT(m_memoryBuffer == NULL);

    if (pBytes) {
        m_memoryBuffer = pBytes;
        m_memoryBufferSize = numBytes;
    } else {
        if (numBytes) {
            m_memoryBufferSize = numBytes;
        } else {
            m_memoryBufferSize = 4096;
        }
        m_memoryBuffer = (uint8_t*)MP4Malloc(m_memoryBufferSize);
    }
    m_memoryBufferPosition = 0;
}

void MP4File::DisableMemoryBuffer(uint8_t** ppBytes, uint64_t* pNumBytes)
{
    ASSERT(m_memoryBuffer != NULL);

    if (ppBytes) {
        *ppBytes = m_memoryBuffer;
    }
    if (pNumBytes) {
        *pNumBytes = m_memoryBufferPosition;
    }

    m_memoryBuffer = NULL;
    m_memoryBufferSize = 0;
    m_memoryBufferPosition = 0;
}

void MP4File::WriteBytes(uint8_t* pBytes, uint32_t numBytes, FILE* pFile)
{
    ASSERT(m_numWriteBits == 0 || m_numWriteBits >= 8);

    if (pBytes == NULL || numBytes == 0) {
        return;
    }

    if (m_memoryBuffer == NULL) {
        if (pFile == NULL) {
            ASSERT(m_pFile);
            if (m_virtual_IO->Write(m_pFile, pBytes, numBytes) != numBytes) {
                throw new MP4Error("error writing bytes via virtual I/O", "MP4WriteBytes");
            }
        } else {
            uint32_t rc = fwrite(pBytes, 1, numBytes, pFile);
            if (rc != numBytes) {
                throw new MP4Error(errno, "MP4WriteBytes");
            }
        }
    } else {
        if (m_memoryBufferPosition + numBytes > m_memoryBufferSize) {
            m_memoryBufferSize = 2 * (m_memoryBufferSize + numBytes);
            m_memoryBuffer = (uint8_t*)
                             MP4Realloc(m_memoryBuffer, m_memoryBufferSize);
        }
        memcpy(&m_memoryBuffer[m_memoryBufferPosition], pBytes, numBytes);
        m_memoryBufferPosition += numBytes;
    }
}

uint64_t MP4File::ReadUInt(uint8_t size)
{
    switch (size) {
    case 1:
        return ReadUInt8();
    case 2:
        return ReadUInt16();
    case 3:
        return ReadUInt24();
    case 4:
        return ReadUInt32();
    case 8:
        return ReadUInt64();
    default:
        ASSERT(false);
        return 0;
    }
}

#if 0
void MP4File::WriteUInt(uint64_t value, uint8_t size)
{
    switch (size) {
    case 1:
        WriteUInt8(value);
    case 2:
        WriteUInt16(value);
    case 3:
        WriteUInt24(value);
    case 4:
        WriteUInt32(value);
    case 8:
        WriteUInt64(value);
    default:
        ASSERT(false);
    }
}
#endif

uint8_t MP4File::ReadUInt8()
{
    uint8_t data;
    ReadBytes(&data, 1);
    return data;
}

void MP4File::WriteUInt8(uint8_t value)
{
    WriteBytes(&value, 1);
}

uint16_t MP4File::ReadUInt16()
{
    uint8_t data[2];
    ReadBytes(&data[0], 2);
    return ((data[0] << 8) | data[1]);
}

void MP4File::WriteUInt16(uint16_t value)
{
    uint8_t data[2];
    data[0] = (value >> 8) & 0xFF;
    data[1] = value & 0xFF;
    WriteBytes(data, 2);
}

uint32_t MP4File::ReadUInt24()
{
    uint8_t data[3];
    ReadBytes(&data[0], 3);
    return ((data[0] << 16) | (data[1] << 8) | data[2]);
}

void MP4File::WriteUInt24(uint32_t value)
{
    uint8_t data[3];
    data[0] = (value >> 16) & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = value & 0xFF;
    WriteBytes(data, 3);
}

uint32_t MP4File::ReadUInt32()
{
    uint8_t data[4];
    ReadBytes(&data[0], 4);
    return ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
}

void MP4File::WriteUInt32(uint32_t value)
{
    uint8_t data[4];
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8) & 0xFF;
    data[3] = value & 0xFF;
    WriteBytes(data, 4);
}

uint64_t MP4File::ReadUInt64()
{
    uint8_t data[8];
    uint64_t result = 0;
    uint64_t temp;

    ReadBytes(&data[0], 8);

    for (int i = 0; i < 8; i++) {
        temp = data[i];
        result |= temp << ((7 - i) * 8);
    }
    return result;
}

void MP4File::WriteUInt64(uint64_t value)
{
    uint8_t data[8];

    for (int i = 7; i >= 0; i--) {
        data[i] = value & 0xFF;
        value >>= 8;
    }
    WriteBytes(data, 8);
}

float MP4File::ReadFixed16()
{
    uint8_t iPart = ReadUInt8();
    uint8_t fPart = ReadUInt8();

    return iPart + (((float)fPart) / 0x100);
}

void MP4File::WriteFixed16(float value)
{
    if (value >= 0x100) {
        throw new MP4Error(ERANGE, "MP4WriteFixed16");
    }

    uint8_t iPart = (uint8_t)value;
    uint8_t fPart = (uint8_t)((value - iPart) * 0x100);

    WriteUInt8(iPart);
    WriteUInt8(fPart);
}

float MP4File::ReadFixed32()
{
    uint16_t iPart = ReadUInt16();
    uint16_t fPart = ReadUInt16();

    return iPart + (((float)fPart) / 0x10000);
}

void MP4File::WriteFixed32(float value)
{
    if (value >= 0x10000) {
        throw new MP4Error(ERANGE, "MP4WriteFixed32");
    }

    uint16_t iPart = (uint16_t)value;
    uint16_t fPart = (uint16_t)((value - iPart) * 0x10000);

    WriteUInt16(iPart);
    WriteUInt16(fPart);
}

float MP4File::ReadFloat()
{
    union {
        float f;
        uint32_t i;
    } u;

    u.i = ReadUInt32();
    return u.f;
}

void MP4File::WriteFloat(float value)
{
    union {
        float f;
        uint32_t i;
    } u;

    u.f = value;
    WriteUInt32(u.i);
}

char* MP4File::ReadString()
{
    uint32_t length = 0;
    uint32_t alloced = 64;
    char* data = (char*)MP4Malloc(alloced);

    do {
        if (length == alloced) {
            data = (char*)MP4Realloc(data, alloced * 2);
            if (data == NULL) return NULL;
            alloced *= 2;
        }
        ReadBytes((uint8_t*)&data[length], 1);
        length++;
    } while (data[length - 1] != 0);

    data = (char*)MP4Realloc(data, length);
    return data;
}

void MP4File::WriteString(char* string)
{
    if (string == NULL) {
        uint8_t zero = 0;
        WriteBytes(&zero, 1);
    } else {
        WriteBytes((uint8_t*)string, strlen(string) + 1);
    }
}

char* MP4File::ReadCountedString(uint8_t charSize, bool allowExpandedCount)
{
    uint32_t charLength;
    if (allowExpandedCount) {
        uint8_t b;
        uint32 ix = 0;
        charLength = 0;
        do {
            b = ReadUInt8();
            charLength += b;
            ix++;
            if (ix > 25)
                throw new MP4Error(ERANGE,
                                   "Counted string too long 25 * 255");
        } while (b == 255);
    } else {
        charLength = ReadUInt8();
    }

    uint32_t byteLength = charLength * charSize;
    char* data = (char*)MP4Malloc(byteLength + 1);
    if (byteLength > 0) {
        ReadBytes((uint8_t*)data, byteLength);
    }
    data[byteLength] = '\0';
    return data;
}

void MP4File::WriteCountedString(char* string,
                                 uint8_t charSize, bool allowExpandedCount,
                                 uint8_t fixedLength)
{
    uint32_t byteLength;
    uint8_t zero[1];

    if (string) {
        byteLength = strlen(string);
        if (fixedLength && (byteLength >= fixedLength)) {
            byteLength = fixedLength-1;
        }
    }
    else {
        byteLength = 0;
    }
    uint32_t charLength = byteLength / charSize;

    if (allowExpandedCount) {
        while (charLength >= 0xFF) {
            WriteUInt8(0xFF);
            charLength -= 0xFF;
        }
        // Write the count
        WriteUInt8(charLength);
    } else {
        if (charLength > 255) {
            throw new MP4Error(ERANGE, "Length is %d", "MP4WriteCountedString", charLength);
        }
        // Write the count
        WriteUInt8(charLength);
    }

    if (byteLength > 0) {
        // Write the string (or the portion that we want to write)
        WriteBytes((uint8_t*)string, byteLength);
    }

    // Write any padding if this is a fixed length counted string
    if (fixedLength) {
        zero[0] = 0;
        while (byteLength < fixedLength-1) {
            WriteBytes(zero, 1);
            byteLength++;
        }
    }
}

uint64_t MP4File::ReadBits(uint8_t numBits)
{
    ASSERT(numBits > 0);
    ASSERT(numBits <= 64);

    uint64_t bits = 0;

    for (uint8_t i = numBits; i > 0; i--) {
        if (m_numReadBits == 0) {
            ReadBytes(&m_bufReadBits, 1);
            m_numReadBits = 8;
        }
        bits = (bits << 1) | ((m_bufReadBits >> (--m_numReadBits)) & 1);
    }

    return bits;
}

void MP4File::FlushReadBits()
{
    // eat any remaining bits in the read buffer
    m_numReadBits = 0;
}

void MP4File::WriteBits(uint64_t bits, uint8_t numBits)
{
    ASSERT(numBits <= 64);

    for (uint8_t i = numBits; i > 0; i--) {
        m_bufWriteBits |=
            (((bits >> (i - 1)) & 1) << (8 - ++m_numWriteBits));

        if (m_numWriteBits == 8) {
            FlushWriteBits();
        }
    }
}

void MP4File::PadWriteBits(uint8_t pad)
{
    if (m_numWriteBits) {
        WriteBits(pad ? 0xFF : 0x00, 8 - m_numWriteBits);
    }
}

void MP4File::FlushWriteBits()
{
    if (m_numWriteBits > 0) {
        WriteBytes(&m_bufWriteBits, 1);
        m_numWriteBits = 0;
        m_bufWriteBits = 0;
    }
}

uint32_t MP4File::ReadMpegLength()
{
    uint32_t length = 0;
    uint8_t numBytes = 0;
    uint8_t b;

    do {
        b = ReadUInt8();
        length = (length << 7) | (b & 0x7F);
        numBytes++;
    } while ((b & 0x80) && numBytes < 4);

    return length;
}

void MP4File::WriteMpegLength(uint32_t value, bool compact)
{
    if (value > 0x0FFFFFFF) {
        throw new MP4Error(ERANGE, "MP4WriteMpegLength");
    }

    int8_t numBytes;

    if (compact) {
        if (value <= 0x7F) {
            numBytes = 1;
        } else if (value <= 0x3FFF) {
            numBytes = 2;
        } else if (value <= 0x1FFFFF) {
            numBytes = 3;
        } else {
            numBytes = 4;
        }
    } else {
        numBytes = 4;
    }

    int8_t i = numBytes;
    do {
        i--;
        uint8_t b = (value >> (i * 7)) & 0x7F;
        if (i > 0) {
            b |= 0x80;
        }
        WriteUInt8(b);
    } while (i > 0);
}

///////////////////////////////////////////////////////////////////////////////

}
} // namespace mp4v2::impl