#pragma once

#include "types.hpp"
#include "algorithm/border.hpp"
#include "container/image.hpp"
#include "math/num.hpp"

#include "algorithm/opt/convolution.3x3-extend.hpp"
#include "algorithm/opt/convolution.5x5-extend.hpp"


namespace impl {

template<typename B, typename T, typename S, index_t Nx, index_t Ny>
void conv_generic(container::image<T>& out, container::image<T> const& in,
                  container::kernel<S, Nx, Ny> const& k)
{
    index_t const dx = (Nx - 1) / 2;
    index_t const dy = (Ny - 1) / 2;

    for (index_t cy = 0; cy < in.shape().y; ++cy) {
        for (index_t cx = 0; cx < in.shape().x; ++cx) {
            out[{cx, cy}] = math::num<T>::zero;

            for (index_t iy = 0; iy < Ny; ++iy) {
                for (index_t ix = 0; ix < Nx; ++ix) {
                    out[{cx, cy}] += B::value(in, {cx - dx + ix, cy - dy + iy}) * k[{ix, iy}];
                }
            }
        }
    }
}

} /* namespace impl */


template<typename B=border::extend, typename T, typename S, index_t Nx, index_t Ny>
void conv(container::image<T>& out, container::image<T> const& in,
          container::kernel<S, Nx, Ny> const& k)
{
    // workaround for partial function template specialization
    if constexpr (Nx == 5 && Ny == 5 && std::is_same_v<B, border::extend>) {
        impl::conv_5x5_extend<T, S>(out, in, k);
    } else if constexpr (Nx == 3 && Ny == 3 && std::is_same_v<B, border::extend>) {
        impl::conv_3x3_extend<T, S>(out, in, k);
    } else {
        impl::conv_generic<B, T, S, Nx, Ny>(out, in, k);
    }
}
