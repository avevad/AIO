#pragma once

#include "AIOxx/event_loop.hpp"
#include "AIOxx/fd.hpp"

namespace AIO {

    template<std::derived_from<StreamSocketFD> BaseFD>
    BufferedStreamFD<BaseFD>::BufferedStreamFD(BaseFD base) : BaseFD(std::move(base)) {
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::size_t> BufferedStreamFD<BaseFD>::read(size_t size, char *data) const {
        return event_loop()->async_execute([this, size, data] mutable -> size_t {
            while (i_sz < size) {
                size_t read_limit = i_beg + i_sz < i_cap ? i_cap - (i_beg + i_sz) : i_cap - i_sz;
                size_t read_pos = i_beg + i_sz < i_cap ? i_beg + i_sz : i_beg + i_sz - i_cap;
                size_t read_amount = event_loop()->await(read_some(read_limit, i_buf.get() + read_pos));
                if (read_amount == 0) {
                    size = i_sz;
                    break;
                }
                i_sz += read_amount;
            }
            if (i_beg + size >= i_cap) {
                size_t n1 = i_cap - i_beg;
                size_t n2 = size - n1;
                std::copy_n(i_buf.get() + i_beg, n1, data);
                std::copy_n(i_buf.get(), n2, data + n1);
                i_beg = n2;
                i_sz -= size;
            } else {
                std::copy_n(i_buf.get() + i_beg, size, data);
                i_beg += size;
                i_sz -= size;
            }
            return size;
        });
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::optional<char>> BufferedStreamFD<BaseFD>::read_byte() const {
        Future<bool> byte_ready;
        if (i_sz == 0) {
            i_beg = 0;
            byte_ready = read_some(i_cap, i_buf.get()).map([this](size_t size) -> bool {
                if (size == 0) {
                    return false;
                }
                i_sz += size;
                return true;
            });
        } else {
            Promise<bool> promise;
            AIO::bind(byte_ready, promise);
            std::move(promise).fulfill(true);
        }
        return std::move(byte_ready).map([this](bool has_byte) -> std::optional<char> {
            if (!has_byte) {
                return std::nullopt;
            }
            char byte = i_buf.get()[i_beg];
            i_beg++;
            i_sz--;
            if (i_beg == i_cap) {
                i_beg = 0;
            }
            return byte;
        });
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::string> BufferedStreamFD<BaseFD>::read_until(char delim, size_t limit) {
        return event_loop()->async_execute([this, delim, limit] () -> std::string {
            std::string result;
            while (true) {
                if (result.size() >= limit) {
                    break;
                }
                if (!result.empty() && result.back() == delim) {
                    break;
                }
                auto maybe_byte = event_loop()->await(read_byte());
                if (!maybe_byte.has_value()) {
                    break;
                }
                result += maybe_byte.value();
            }
            return result;
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

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::size_t> BufferedStreamFD<BaseFD>::read_some(size_t size, char *data) const {
        return BaseFD::read(size, data);
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    Future<std::size_t> BufferedStreamFD<BaseFD>::write_some(size_t size, const char *data) const {
        return BaseFD::write(size, data);
    }

    template<std::derived_from<StreamSocketFD> BaseFD>
    BasicEventLoop *BufferedStreamFD<BaseFD>::event_loop() const {
        return BaseFD::loop;
    }

} // namespace AIO
