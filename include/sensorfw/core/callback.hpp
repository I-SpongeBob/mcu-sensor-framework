/**
 * @file callback.hpp
 * @brief Zero-allocation replacement for std::function.
 *
 * std::function may heap-allocate and drags in exceptions/RTTI, so it is
 * unusable in most firmware. A Callback is exactly two pointers
 * (free function + context) and is trivially copyable, so it can live in a
 * static array inside a Publisher.
 */
#ifndef SENSORFW_CORE_CALLBACK_HPP
#define SENSORFW_CORE_CALLBACK_HPP

#include <stddef.h>

namespace sensorfw {

template <typename Arg>
class Callback {
public:
    typedef void (*Fn)(void* context, Arg arg);

    Callback() : fn_(NULL), context_(NULL) {}
    Callback(Fn fn, void* context) : fn_(fn), context_(context) {}

    /**
     * @brief Bind a non-static member function without any allocation.
     *
     * Usage: Callback<const Measurement&>::bind<GuiView, &GuiView::onMeasurement>(&gui)
     *
     * The member pointer is a template argument, so the compiler devirtualises
     * the call into a plain function - same cost as a C callback, but type safe.
     */
    template <typename C, void (C::*Method)(Arg)>
    static Callback bind(C* object) {
        return Callback(&memberThunk<C, Method>, object);
    }

    /** @brief Bind a free function taking (void*, Arg). */
    static Callback fromFunction(Fn fn, void* context) { return Callback(fn, context); }

    bool valid() const { return fn_ != NULL; }
    void operator()(Arg arg) const { if (fn_ != NULL) { fn_(context_, arg); } }

private:
    template <typename C, void (C::*Method)(Arg)>
    static void memberThunk(void* context, Arg arg) {
        (static_cast<C*>(context)->*Method)(arg);
    }

    Fn    fn_;
    void* context_;
};

} // namespace sensorfw

#endif // SENSORFW_CORE_CALLBACK_HPP
