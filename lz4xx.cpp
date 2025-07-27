#include "lz4xx.h"

#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <lz4.h>
#include <lz4file.h>

inline void info(const char* msg) {
    std::cout << "info: " << msg << std::endl;
}

inline void __debug__(const char* msg,const char* file, int line) {
    static std::recursive_mutex mtx;
    mtx.lock();
    std::cout << "debug: " << msg << " @"<< file<< "#"<< line << std::endl;
    mtx.unlock();
}

#ifdef _DEBUG
#define debug(msg) __debug__(msg,__FILE__,__LINE__)
#else
#define debug()
#endif

namespace lz4xx
{

constexpr auto MB = 1048576;
constexpr auto SZ_BUFFER = 32 * MB;     // buffer size 32MB
using buffer_t = std::vector<uint8_t>;

void cast_preferences(
    const lz4xx::preferences& pfs, size_t contentSize,
    LZ4F_preferences_t& out)
{
    out.compressionLevel = pfs.level % 16;         // 压缩级别 (1-16)
    out.autoFlush = pfs.autoFlush;
    out.favorDecSpeed = pfs.favorDecSpeed;
    out.frameInfo.contentChecksumFlag = pfs.frame.checksumContent ? LZ4F_contentChecksumEnabled : LZ4F_noContentChecksum;
    out.frameInfo.blockChecksumFlag = pfs.frame.checksumBlock ? LZ4F_blockChecksumEnabled : LZ4F_noBlockChecksum;
    out.frameInfo.contentSize = contentSize;

    LZ4F_blockSizeID_t bs = out.frameInfo.blockSizeID;
    switch (pfs.frame.blockSize) {
    case BS_Default: bs = LZ4F_default; break;
    case BS_Max64KB: bs = LZ4F_max64KB; break;
    case BS_Max256KB: bs = LZ4F_max256KB; break;
    case BS_Max1MB: bs = LZ4F_max1MB; break;
    case BS_Max4MB: bs = LZ4F_max4MB; break;
    }
    out.frameInfo.blockSizeID = bs;


    LZ4F_blockMode_t bm = out.frameInfo.blockMode;
    switch (pfs.frame.blockMode) {
    case BM_Linked: bm = LZ4F_blockLinked; break;
    case BM_Independent: bm = LZ4F_blockIndependent; break;
    }
    out.frameInfo.blockMode = bm;
}

template<typename T>
inline size_t bufsz(T size)
{
    auto ret = MB;
    if(size > 1024 * MB)    // 1GB
        ret = 20 * MB;
    else if(size > 50 * MB)
        ret = 10 * MB;
    return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/// progress
progress::progress()
    : _cb(nullptr)
{}

progress::progress(const cb &cb)
    : _cb(cb)
{}

void progress::attach(const cb &cb) {
    _cb = cb;
}

void progress::set(int chunk, float value) {
    if(_cb) _cb(chunk,value);
}

struct base_prv {
    bool begun = false;
    size_t content_size;      // source data size
    std::recursive_mutex mtx;
    iwriter& wt;

    std::string last_error;     // last error message
    std::stringstream ss;       // log format stream

    buffer_t buf_in;            // input buffer
    buffer_t buf_out;           // output buffer
    size_t cur_buf_in = 0;      // input buffer current size

    inline base_prv(iwriter& wt)
        : wt(wt)
    {}
    virtual ~base_prv() {}

    // maybe read header in "begin", so "data" and "size" maybe modify
    virtual bool begin(const uint8_t*& data,size_t& size) = 0;

    // commit data to "buf_in", will call "update" when "sz_buf_in" >= buf_in.size()
    bool commit(const uint8_t *data, size_t size);

    virtual bool update() = 0;
};

inline void __error__(base_prv* d,const char* msg,const char* file, int line) {
    d->ss.clear();
    d->ss << "error: " << msg << " @"<< file<< "#"<< line;
    d->last_error = d->ss.str();
    std::cerr << d->ss.str() << std::endl;
}

inline void __error__(base_prv* d,const std::string& msg,const char* file, int line) {
    __error__(d,msg.c_str(),file,line);
}

inline void __error__(base_prv* d,const std::stringstream& ss,const char* file, int line) {
    __error__(d,ss.str().c_str(),file,line);
}

#define error(d,msg) __error__(d,msg,__FILE__,__LINE__)

template<>
struct context<Compress>::prv : public base_prv
{
    LZ4F_compressionContext_t ctx;
    LZ4F_preferences_t pfs;

    inline prv(iwriter& wt) : base_prv(wt) {
        LZ4F_createCompressionContext(&ctx, LZ4F_VERSION);
    }

    inline ~prv() {
        LZ4F_freeCompressionContext(ctx);
    }

    bool begin(const uint8_t*&,size_t& size) override;
    bool update() override;
    bool finish();
};

template<>
struct context<Decompress>::prv : public base_prv
{
    LZ4F_decompressionContext_t ctx;

    inline prv(iwriter& wt) : base_prv(wt) {
        LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
    }

    inline ~prv() {
        LZ4F_freeDecompressionContext(ctx);
    }

    bool begin(const uint8_t*&,size_t& size) override;
    bool update() override;
    bool finish();
};

template<Mode MO>
context<MO>::context(prv *d) : d(d)
{}

template<Mode MO>
context<MO>::context() : d(nullptr)
{}

template<Mode MO>
context<MO>::context(context && o) : d(o.d)
{
    o.d = nullptr;
}

template<Mode MO>
context<MO>::~context()
{
    if(d) delete d;
}

template<Mode MO>
bool context<MO>::update(const uint8_t *data, size_t size)
{
    return d->commit(data,size);
}

template<Mode MO>
bool context<MO>::finish()
{
    d->update();
    return d->finish();
}

template<Mode MO>
void context<MO>::reset()
{
    d->begun = false;
}

template<Mode MO>
const std::string &context<MO>::lasterror() const
{
    return d->last_error;
}

template<Mode MO>
context<MO> &context<MO>::operator=(context && o)
{
    if(d) delete d;
    d = o.d;
    o.d = nullptr;
    return *this;
}

template class context<Compress>;
template class context<Decompress>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/// base_prv
bool base_prv::commit(const uint8_t *data, size_t size)
{
    if(!data || size < 1)
        return true;
    std::lock_guard<std::recursive_mutex> lk(mtx);

    if(!begun) {
        begun = begin(data,size);
        if(!begun) return false;
    }
    if(size < 1)
        return true;

    const auto capacity = buf_in.size();
    const auto total = cur_buf_in + size;
    if(total < capacity)
    {
        memcpy(buf_in.data() + cur_buf_in, data, size);
        cur_buf_in = total;
        return true;
    }
    else
    {
        auto offset = capacity - cur_buf_in;
        memcpy(buf_in.data() + cur_buf_in, data, offset);

        cur_buf_in = capacity;
        if(!update())
            return false;
        return commit(data + offset, size - offset);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/// context<Compress>::prv
bool context<Compress>::prv::begin(const uint8_t*& data,size_t& size)
{
    // try to set a smaller buffer size
    if(content_size < SZ_BUFFER)
    {
        buf_in.resize(content_size);
        buf_out.reserve(content_size + bufsz(content_size));
    }
    else
    {
        buf_in.resize(SZ_BUFFER);
        buf_out.reserve(SZ_BUFFER);
    }

    auto sz_out = LZ4F_compressBound(content_size, &pfs);
    if(sz_out < LZ4F_HEADER_SIZE_MIN)
    {
        error(this,"bound size to small!");
        return false;
    }
    // output capacity, and some buffer size. only work for buffer output
    wt.reserve(sz_out + bufsz(sz_out));

    const size_t sz_bound = LZ4F_compressBound(0, &pfs);    // header max size
    buffer_t header(sz_bound);
    auto sz_header = LZ4F_compressBegin(ctx,header.data(),sz_bound,&pfs);
    if (LZ4F_isError(sz_header)) {
        ss << "frame header error:" << LZ4F_getErrorName(sz_header) << std::endl
           << "  maybe your preferences not right!";
        error(this,ss);
        return false;
    }
    wt.write(header.data(), sz_header);
    return true;
}

bool context<Compress>::prv::update()
{
    std::lock_guard<std::recursive_mutex> lk(mtx);
    if(cur_buf_in < 1)
        return true;

    buf_out.resize(LZ4F_compressBound(cur_buf_in, &pfs));
    const size_t sz_com = LZ4F_compressUpdate(
        ctx,
        buf_out.data(),
        buf_out.size(),
        buf_in.data(),
        cur_buf_in,
        nullptr
        );
    if (LZ4F_isError(sz_com)) {
        error(this,LZ4F_getErrorName(sz_com));
        cur_buf_in = 0;
        return false;
    }
    wt.write(buf_out.data(), sz_com);

    cur_buf_in = 0;
    return true;
}

bool context<Compress>::prv::finish()
{
    std::lock_guard<std::recursive_mutex> lk(mtx);
    begun = false;

    const size_t sz_end = LZ4F_compressEnd(
        ctx,
        buf_out.data(),
        buf_out.size(),
        nullptr
        );
    if (LZ4F_isError(sz_end)) {
        ss << "error at the end:" << LZ4F_getErrorName(sz_end);
        error(this,ss);
        return false;
    }

    wt.write(buf_out.data(), sz_end);
    return sz_end >= 4;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/// context<Decompress>::prv
bool context<Decompress>::prv::begin(const uint8_t*& data,size_t &size)
{
    if(size < LZ4F_HEADER_SIZE_MAX)
    {
        ss << "data size must >= \"" << LZ4F_HEADER_SIZE_MAX << "\" when first time calling \"update\"";
        error(this,ss);
        return false;
    }
    size_t sz_read = size;
    LZ4F_frameInfo_t info;
    size_t ret = LZ4F_getFrameInfo(ctx, &info, data, &sz_read);
    if (LZ4F_isError(ret)) {
        ss << "error when read frame header:" << LZ4F_getErrorName(ret);
        error(this,ss);
        return false;
    }
    // output capacity, and some buffer size. only work for buffer output
    wt.reserve(size_t(info.contentSize) + bufsz(info.contentSize));

    // try to set a smaller buffer size
    if(info.contentSize < SZ_BUFFER)
    {
        buf_in.resize(size_t(info.contentSize));
        buf_out.reserve(size_t(info.contentSize) + bufsz(info.contentSize));
    }
    else
    {
        buf_in.resize(SZ_BUFFER);
        buf_out.reserve(SZ_BUFFER);
    }

    // 'sz_read' maybe less than 'size'
    data += sz_read;
    size -= sz_read;
    return true;
}

bool context<Decompress>::prv::update()
{
    if(cur_buf_in < 1)
        return true;

    auto data = buf_in.data();
    std::lock_guard<std::recursive_mutex> lk(mtx);

    size_t remain = cur_buf_in;
    size_t pos = 0;
    while(remain > 0)
    {
        size_t sz_out = buf_out.capacity();
        buf_out.resize(sz_out);
        const size_t ret = LZ4F_decompress(
            ctx,
            buf_out.data(),
            &sz_out,
            data + pos,
            &cur_buf_in,
            nullptr
            );
        if (LZ4F_isError(ret)) {
            error(this,LZ4F_getErrorName(ret));
            return false;
        }
        remain -= cur_buf_in;
        pos += cur_buf_in;
        cur_buf_in = remain;

        wt.write(buf_out.data(), sz_out);
    }
    return true;
}

bool context<Decompress>::prv::finish()
{
    mtx.lock();
    begun = false;
    mtx.unlock();
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Functions
context<Compress> compress(size_t contentSize, iwriter &wt, const preferences & pfs)
{
    auto d = new context<Compress>::prv(wt);
    d->content_size = contentSize;
    cast_preferences(pfs,contentSize,d->pfs);
    return d;
}

context<Decompress> decompress(iwriter &wt)
{
    return new context<Decompress>::prv(wt);
}

float percent(int64_t current,int64_t total)
{ return int(current / double(total) * 10000) / 100.0f; }

bool compress(
    ireader &rd, iwriter &wt,
    progress* pgs,
    const preferences & pfs
    )
{
    const auto total = rd.seek(-1);
    rd.seek(0);
    auto ctx = lz4xx::compress(total,wt,pfs);

    lz4xx::buffer_t buffer(total < SZ_BUFFER ? total : SZ_BUFFER);

    std::cout << "#### compression start ####" << std::endl;
    size_t current = 0;
    auto before = std::chrono::system_clock::now();

    bool ok = false;
    size_t sz_read = 0;
    while((sz_read = rd.read(buffer.data(),buffer.size())) > 0)
    {
        ok = ctx.update(buffer.data(),sz_read);
        if(!ok) break;

        current += sz_read;
        if(current > total)
            current = total;
        auto now = std::chrono::system_clock::now();
        if(current == total || (now - before).count() > 500)   // 500 ms
        {
            auto v = percent(current,total);
            std::cout << percent(current,total) << " %" << std::endl;
            if(pgs) pgs->set(0,v);
            before = now;
        }
    }
    ok = ok && ctx.finish();

    std::cout << "#### compression finish ####" << std::endl;

    if(!ok) pgs->last_error = ctx.lasterror();
    return ok;
}

bool decompress(
    ireader &rd, iwriter &wt,
    progress* pgs
    )
{
    const auto beg = rd.pos();  // the begin position
    const auto total = rd.seek(-1);
    rd.seek(beg);
    auto ctx = lz4xx::decompress(wt);

    lz4xx::buffer_t buffer(total < SZ_BUFFER ? total : SZ_BUFFER);

    std::cout << "#### decompression start ####" << std::endl;
    size_t current = 0;
    auto before = std::chrono::system_clock::now();

    bool ok = false;
    size_t sz_read = 0;
    while((sz_read = rd.read(buffer.data(),buffer.size())) > 0)
    {
        ok = ctx.update(buffer.data(),sz_read);
        if(!ok) break;

        current += sz_read;
        if(current > total)
            current = total;
        auto now = std::chrono::system_clock::now();
        if(current == total || (now - before).count() > 500)   // 500 ms
        {
            auto v = percent(current,total);
            std::cout << percent(current,total) << " %" << std::endl;
            if(pgs) pgs->set(0,v);
            before = now;
        }
    }
    ok = ok && ctx.finish();

    std::cout << "#### decompression finish ####" << std::endl;

    if(!ok) pgs->last_error = ctx.lasterror();
    return ok;
}

}

