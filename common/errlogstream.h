#ifndef ERRLOGSTREAM_H
#define ERRLOGSTREAM_H

#include <streambuf>
#include <ostream>
#include <vector>
#include <algorithm>

#include <assert.h>

#include <errlog.h>

//! output only stream buffer which write to the epics errlog
struct errlog_streambuf : public std::streambuf
{
    typedef std::vector<char> buffer_t;

    errlog_streambuf(bool block=false, size_t blen=126)
        :std::streambuf()
        ,p_outbuf(std::max(blen, size_t(16u))+2)
        ,p_block(block)
    {
        p_reset();
    }
    virtual ~errlog_streambuf() {sync();}
    virtual int_type overflow(int_type c)
    {
        size_t nwrite = pptr()-pbase();
        assert(nwrite<p_outbuf.size()-1);
        if(c!=traits_type::eof()) {
            p_outbuf[nwrite++] = traits_type::to_char_type(c);
        }
        p_flush(nwrite);
        p_reset();
        return traits_type::not_eof(c);
    }

    //! flush local buffer to global errlog buffer.
    //! if block=true then also calls errlogFlush()
    virtual int sync()
    {
        size_t nwrite = pptr()-pbase();
        assert(nwrite<p_outbuf.size()-1);
        p_flush(nwrite);
        p_reset();
        return 0;
    }

private:
    void p_reset()
    {
        char *B = &p_outbuf[0];
        setp(B, B+p_outbuf.size()-2); // reserve one char for overflow()s arg and one for nil
    }
    void p_flush(size_t nwrite)
    {
        if(nwrite) {
            p_outbuf[nwrite++] = '\0';
            errlogMessage(&p_outbuf[0]);
            if(p_block)
                errlogFlush();
        }
    }

    buffer_t p_outbuf;
    bool p_block;
};

struct errlog_ostream : public std::ostream
{
    errlog_ostream(bool block=false, size_t blen=126)
        :std::ostream(&p_strm)
        ,p_strm(block, blen)
    {}
    virtual ~errlog_ostream() {}
private:
    errlog_streambuf p_strm;
};

#endif // ERRLOGSTREAM_H
