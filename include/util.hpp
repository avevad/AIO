#pragma once

#include <iostream>
#include <source_location>
#include <string>
#include <optional>

namespace AIO {

    [[noreturn]] void
    assertion_failed(const std::string &what, std::source_location where = std::source_location::current());

    template<typename Derived, typename Derived1>
    class Bound {
    public:
        Bound() = default;

        Bound(const Bound &) = delete;
        Bound &operator=(const Bound &) = delete;

        Bound(Bound &&other) noexcept: bond(other.bond) {
            other.bond.reset();
            if (auto bound = get_bound_base_ptr()) {
                bound->bond = static_cast<Derived *>(this);
            }
        }

        Bound &operator=(Bound &&other) noexcept {
            if (&other == this) {
                return *this;
            }

            if (auto bound = get_bound_base_ptr()) {
                bound->bond = nullptr;
            }

            bond = other.bond;
            other.bond.reset();
            if (auto bound = get_bound_base_ptr()) {
                bound->bond = static_cast<Derived *>(this);
            }

            return *this;
        }

        ~Bound() {
            if (auto bound = get_bound_base_ptr()) {
                bound->bond = nullptr;
            }
        }

    protected:
        bool is_bound() {
            return bond.has_value();
        }

        Derived1 *get_bound_ptr() {
            if (!is_bound()) {
                assertion_failed("accessing non-existent bond");
            }
            return bond.value();
        }

        Derived1 &get_bound_obj() {
            if (!is_bound()) {
                assertion_failed("accessing non-existent bond");
            }
            return *bond.value();
        }

        void unbind() {
            if (!is_bound()) {
                assertion_failed("attempt to unbind unbound object");
            }
            get_bound_base_ptr()->bond = nullptr;
            bond = nullptr;
        }

    private:
        template<typename A, typename B>
            requires(std::derived_from<A, Bound<A, B>> && std::derived_from<B, Bound<B, A>>)
        friend void bind(A &a, B &b);

        friend class Bound<Derived1, Derived>;

        Bound<Derived1, Derived> *get_bound_base_ptr() {
            return bond.has_value() ? static_cast<Bound<Derived1, Derived> *>(bond.value()) : nullptr;
        }

        std::optional<Derived1 *> bond = std::nullopt;
    };

    template<typename A, typename B>
        requires(std::derived_from<A, Bound<A, B>> && std::derived_from<B, Bound<B, A>>)
    void bind(A &a, B &b) {
        if (a.bond.has_value() || b.bond.has_value()) {
            assertion_failed("binding already bound object");
        }

        a.bond = &b;
        b.bond = &a;
    }
}
