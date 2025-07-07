#pragma once

#include "AIOxx/fd.hpp"

namespace AIO {
    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::size_t> BufferedStreamFD<BaseFD>::read(size_t size, char *data) const {
        return BaseFD::loop->async_execute([this, size, data] -> size_t {
            size_t read_total = std::min(size, i_sz);
            if (read_total) {
                std::copy_n(i_buf.get(), read_total, data);
                std::copy_n(i_buf.get() + read_total, i_sz - read_total, i_buf.get());
                i_sz -= read_total;
            }
            while (read_total != size) {
                size_t read_size = BaseFD::loop->await(BaseFD::read(size - read_total, data + read_total));
                if (read_size == 0) {
                    break;
                }
                read_total += read_size;
            }
            return read_total;
        });
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::size_t> BufferedStreamFD<BaseFD>::write(size_t size, const char *data) const {
        return BaseFD::loop->async_execute([this, size, data] -> size_t {
            if (o_sz + size <= o_cap) {
                std::copy_n(data, size, o_buf.get() + o_sz);
                o_sz += size;
                return size;
            }
            if (!BaseFD::loop->await(flush())) {
                return 0;
            }
            size_t write_total = 0;
            while (write_total != size) {
                size_t write_size = BaseFD::loop->await(BaseFD::write(size - write_total, data + write_total));
                if (write_size == 0) {
                    break;
                }
                write_total += write_size;
            }
            return write_total;
        });
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    std::string BufferedStreamFD<BaseFD>::read_until(char delim, size_t) {
        std::string result;
        while (true) {
            std::string_view buf_view(i_buf.get(), i_sz);
            size_t delim_pos = buf_view.find(delim);
            if (delim_pos == std::string_view::npos) {
                result += buf_view;
                i_cap = BaseFD::loop->await(BaseFD::read(i_cap, i_buf.get()));
                if (i_cap == 0) {
                    break;
                }
            } else {
                result += buf_view.substr(0, delim_pos + 1);
                std::copy_n(i_buf.get() + delim_pos + 1, i_sz - delim_pos - 1, i_buf.get());
                i_sz -= delim_pos + 1;
            }
        }
        return result;
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<bool> BufferedStreamFD<BaseFD>::flush() const {
        return BaseFD::loop->async_execute([this] -> bool {
            size_t write_total = 0;
            while (write_total != o_sz) {
                size_t write_size = BaseFD::loop->await(BaseFD::write(o_sz - write_total, o_buf.get() + write_total));
                if (write_size == 0) {
                    return false;
                }
                write_total += write_size;
            }
            return true;
        });
    }
} // namespace AIO
