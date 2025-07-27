#include <lz4xx.h>
#include <fstream>

constexpr auto MB = 1048576;
constexpr auto SZ_BUFFER = 23 * 1048000;     // buffer size 32MB

#define FILENAME "H:/AdobePhotoshop_2020_21.2.12.215_x64_Green.7z"

int main()
{

    if(true)
    {
        std::ifstream fi(FILENAME,std::ios::binary | std::ios::ate);
        // std::ifstream fi("E:/cs.las",std::ios::binary);
        if(!fi.is_open())
            return 1;
        const auto size = fi.tellg();
        fi.seekg(0,std::ios::beg);

        std::ofstream fo(FILENAME ".lz4",std::ios::binary);
        // std::ofstream fo("E:/cs.las.lz4",std::ios::binary);
        if(!fo.is_open())
            return 1;

        if(true)
        {
            lz4xx::reader_stream rd(fi);
            lz4xx::writer_stream wt(fo);

            lz4xx::buffer_t buf;
            lz4xx::writer_buffer wtt(buf);
            lz4xx::compress(rd,wtt);

            lz4xx::reader_buffer rdb(&buf);
            lz4xx::decompress(rdb,wt);
        }
        else
        {
            lz4xx::writer_stream wt(fo);
            auto ctx = lz4xx::compress(size,wt);
            lz4xx::buffer_t buffer(SZ_BUFFER);

            bool failed = false;
            while(!fi.eof())
            {
                fi.read((char*)buffer.data(),SZ_BUFFER);
                auto sz_read = fi.gcount();
                failed = !ctx.update(buffer.data(),sz_read);
                if(failed) break;
            }

            failed = failed || !ctx.finish();
        }

        fi.close();
        fo.close();
    }


    if(false)
    {
        std::ifstream fi(FILENAME ".lz4",std::ios::binary);
        if(!fi.is_open())
            return 1;

        std::ofstream fo(FILENAME ".rar",std::ios::binary);
        if(!fo.is_open())
            return 1;

        if(true)
        {
            lz4xx::reader_stream rd(fi);
            lz4xx::writer_stream wt(fo);
            lz4xx::decompress(rd,wt);
        }
        else
        {
            lz4xx::writer_stream wt(fo);
            auto ctx = lz4xx::decompress(wt);
            lz4xx::buffer_t buffer(SZ_BUFFER);

            bool failed = false;
            while(!fi.eof())
            {
                fi.read((char*)buffer.data(),SZ_BUFFER);
                auto sz_read = fi.gcount();
                failed = !ctx.update(buffer.data(),sz_read);
                if(failed) break;
            }
            failed = failed || !ctx.finish();
        }

        fi.close();
        fo.close();
    }
    return 0;
}
