// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STREAMS_H
#define BITCOIN_STREAMS_H

#include <algorithm>
#include <assert.h>
#include <ios>
#include <limits>
#include <map>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <utility>
#include <vector>
#include <support/allocators/zeroafterfree.h>
#include <serialize.h>

/* Minimal stream for overwriting and/or appending to an existing byte vector
 *
 * The referenced vector will grow as necessary
 */
class CVectorWriter
{
public:

    /*
     * @param[in]  nTypeIn Serialization Type
     * @param[in]  nVersionIn Serialization Version (including any flags)
     * @param[in]  vchDataIn  Referenced byte vector to overwrite/append
     * @param[in]  nPosIn Starting position. Vector index where writes should start. The vector will initially
     *                    grow as necessary to max(nPosIn, vec.size()). So to append, use vec.size().
     */
    CVectorWriter(int nTypeIn,
                  int nVersionIn,
                  std::vector<unsigned char>& vchDataIn,
                  size_t nPosIn)
        : nType(nTypeIn)
        , nVersion(nVersionIn)
        , vchData(vchDataIn)
        , nPos(nPosIn)
    {
        if(nPos > vchData.size())
            vchData.resize(nPos);
    }
    /*
     * (other params same as above)
     * @param[in]  args  A list of items to serialize starting at nPosIn.
     */
    template <typename... Args>
    CVectorWriter(int nTypeIn,
                  int nVersionIn,
                  std::vector<unsigned char>& vchDataIn,
                  size_t nPosIn,
                  Args&&... args)
        : CVectorWriter(nTypeIn, nVersionIn, vchDataIn, nPosIn)
    {
        ::SerializeMany(*this, std::forward<Args>(args)...);
    }
    void write(const char* pch, size_t nSize)
    {
        assert(nPos <= vchData.size());
        size_t nOverwrite = std::min(nSize, vchData.size() - nPos);
        if (nOverwrite)
            memcpy(vchData.data() + nPos, reinterpret_cast<const unsigned char*>(pch), nOverwrite);
        if (nOverwrite < nSize)
            vchData.insert(vchData.end(), reinterpret_cast<const unsigned char*>(pch) + nOverwrite,
                    reinterpret_cast<const unsigned char*>(pch) + nSize);
        nPos += nSize;
    }
    template<typename T>
    CVectorWriter& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }
    int GetVersion() const
    {
        return nVersion;
    }
    int GetType() const
    {
        return nType;
    }
    void seek(size_t nSize)
    {
        nPos += nSize;
        if(nPos > vchData.size())
            vchData.resize(nPos);
    }
private:
    const int                   nType;
    const int                   nVersion;
    std::vector<unsigned char>& vchData;
    size_t                      nPos;
};



/** Double ended buffer combining vector and stream-like interfaces.
 *
 * >> and << read and write unformatted data using the above serialization templates.
 * Fills with data in linear time; some stringstream implementations take N^2 time.
 */
class CDataStream
{
protected:
    typedef CSerializeData  vector_type;
    vector_type             vch;
    unsigned int            nReadPos;

    int                     nType;
    int                     nVersion;
public:

    typedef vector_type::allocator_type   allocator_type;
    typedef vector_type::size_type        size_type;
    typedef vector_type::difference_type  difference_type;
    typedef vector_type::reference        reference;
    typedef vector_type::const_reference  const_reference;
    typedef vector_type::value_type       value_type;
    typedef vector_type::iterator         iterator;
    typedef vector_type::const_iterator   const_iterator;
    typedef vector_type::reverse_iterator reverse_iterator;

    explicit CDataStream(int nTypeIn,
                         int nVersionIn)
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const_iterator pbegin,
                const_iterator pend,
                int nTypeIn,
                int nVersionIn)
        : vch(pbegin, pend)
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const char* pbegin,
                const char* pend,
                int nTypeIn,
                int nVersionIn)
        : vch(pbegin, pend)
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const vector_type& vchIn,
                int nTypeIn,
                int nVersionIn)
        : vch(vchIn.begin()
        , vchIn.end())
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const std::vector<char>& vchIn,
                int nTypeIn,
                int nVersionIn)
        : vch(vchIn.begin()
        , vchIn.end())
    {
        Init(nTypeIn, nVersionIn);
    }

    CDataStream(const std::vector<unsigned char>& vchIn,
                int nTypeIn,
                int nVersionIn)
        : vch(vchIn.begin()
        , vchIn.end())
    {
        Init(nTypeIn, nVersionIn);
    }

    template <typename... Args>
    CDataStream(int nTypeIn,
                int nVersionIn, Args&&... args)
    {
        Init(nTypeIn, nVersionIn);
        ::SerializeMany(*this, std::forward<Args>(args)...);
    }

    void Init(int nTypeIn, int nVersionIn)
    {
        nReadPos = 0;
        nType = nTypeIn;
        nVersion = nVersionIn;
    }

    CDataStream& operator+=(const CDataStream& b)
    {
        vch.insert(vch.end(), b.begin(), b.end());
        return *this;
    }

    friend CDataStream operator+(const CDataStream& a, const CDataStream& b)
    {
        CDataStream ret = a;
        ret += b;
        return (ret);
    }

    std::string str() const
    {
        return (std::string(begin(), end()));
    }


    //
    // Vector subset
    //
    const_iterator begin() const
    {
        return vch.begin() + nReadPos;
    }
    iterator begin()
    {
        return vch.begin() + nReadPos;
    }
    const_iterator end() const
    {
        return vch.end();
    }
    iterator end()
    {
        return vch.end();
    }
    size_type size() const
    {
        return vch.size() - nReadPos;
    }
    bool empty() const
    {
        return vch.size() == nReadPos;
    }
    void resize(size_type n, value_type c=0)
    {
        vch.resize(n + nReadPos, c);
    }
    void reserve(size_type n)
    {
        vch.reserve(n + nReadPos);
    }
    const_reference operator[](size_type pos) const
    {
        return vch[pos + nReadPos];
    }
    reference operator[](size_type pos)
    {
        return vch[pos + nReadPos];
    }
    void clear()
    {
        vch.clear(); nReadPos = 0;
    }
    iterator insert(iterator it, const char x=char())
    {
        return vch.insert(it, x);
    }
    void insert(iterator it, size_type n, const char x)
    {
        vch.insert(it, n, x);
    }
    value_type* data()
    {
        return vch.data() + nReadPos;
    }
    const value_type* data() const
    {
        return vch.data() + nReadPos;
    }

    void insert(iterator it, std::vector<char>::const_iterator first, std::vector<char>::const_iterator last)
    {
        if (last == first) return;
        assert(last - first > 0);
        if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
        {
            // special case for inserting at the front when there's room
            nReadPos -= (last - first);
            memcpy(&vch[nReadPos], &first[0], last - first);
        }
        else
            vch.insert(it, first, last);
    }

    void insert(iterator it, const char* first, const char* last)
    {
        if (last == first) return;
        assert(last - first > 0);
        if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
        {
            // special case for inserting at the front when there's room
            nReadPos -= (last - first);
            memcpy(&vch[nReadPos], &first[0], last - first);
        }
        else
            vch.insert(it, first, last);
    }

    //
    // Stream subset
    //
    bool eof() const
    {
        return size() == 0;
    }
    CDataStream* rdbuf()
    {
        return this;
    }
    int in_avail() const
    {
        return size();
    }

    void SetType(int n)
    {
        nType = n;
    }
    int GetType() const
    {
        return nType;
    }
    void SetVersion(int n)
    {
        nVersion = n;
    }
    int GetVersion() const
    {
        return nVersion;
    }

    void read(char* pch, size_t nSize)
    {
        if (nSize == 0) return;

        // Read from the beginning of the buffer
        unsigned int nReadPosNext = nReadPos + nSize;
        if (nReadPosNext > vch.size())
            throw std::ios_base::failure("CDataStream::read(): end of data");
        memcpy(pch, &vch[nReadPos], nSize);
        if (nReadPosNext == vch.size())
        {
            nReadPos = 0;
            vch.clear();
            return;
        }
        nReadPos = nReadPosNext;
    }

    void write(const char* pch, size_t nSize)
    {
        // Write to the end of the buffer
        vch.insert(vch.end(), pch, pch + nSize);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        // Special case: stream << stream concatenates like stream += stream
        if (!vch.empty())
            s.write((char*)vch.data(), vch.size() * sizeof(value_type));
    }

    template<typename T>
    CDataStream& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    CDataStream& operator>>(T&& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }
};

#endif // BITCOIN_STREAMS_H
