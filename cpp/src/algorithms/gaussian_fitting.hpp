#pragma once

#include "math.hpp"

#include "math/mat6.hpp"
#include "math/sle6.hpp"
#include "math/vec6.hpp"
#include "math/num.hpp"

#include <array>


namespace gfit {

using math::mat6_t;
using math::vec6_t;


template<class T>
inline constexpr auto const range = vec2<T> { static_cast<T>(1), static_cast<T>(1) };


struct bbox {
    index_t xmin, xmax;
    index_t ymin, ymax;
};

template<class T>
struct parameters {
    bool        valid;      // flag to invalidate parameters
    T           scale;      // alpha
    vec2<T>     mean;       // mu
    mat2s<T>    prec;       // precision matrix, aka. inverse covariance matrix, aka. sigma^-1
    bbox        bounds;     // local bounds for sampling
    image<T>    weights;    // local weights for sampling
};


namespace impl {

/**
 * gaussian_like() - 2D Gaussian probability density function without normalization.
 * @x:    Position at which to evaluate the function.
 * @mean: Mean of the Gaussian.
 * @prec: Precision matrix, i.e. the invariance of the covariance matrix.
 */
template<class T>
auto gaussian_like(vec2<T> x, vec2<T> mean, mat2s<T> prec) -> T
{
    return std::exp(-xtmx(prec, x - mean) / static_cast<T>(2));
}


template<class T, class S>
inline void assemble_system(mat6_t<S>& m, vec6_t<S>& rhs, bbox const& b, image<T> const& data, image<S> const& w)
{
    auto const eps = std::numeric_limits<S>::epsilon();

    auto const scale = vec2<S> {
        static_cast<S>(2) * range<S>.x / static_cast<S>(data.shape().x),
        static_cast<S>(2) * range<S>.y / static_cast<S>(data.shape().y),
    };

    std::fill(m.data.begin(), m.data.end(), zero<S>());
    std::fill(rhs.data.begin(), rhs.data.end(), zero<S>());

    for (index_t iy = b.ymin; iy <= b.ymax; ++iy) {
        for (index_t ix = b.xmin; ix <= b.xmax; ++ix) {
            auto const x = static_cast<S>(ix) * scale.x - range<S>.x;
            auto const y = static_cast<S>(iy) * scale.y - range<S>.y;

            auto const d = w[{ix - b.xmin, iy - b.ymin}] * static_cast<S>(data[{ix, iy}]);
            auto const v = std::log(d + eps) * d * d;

            rhs[0] += v * x * x;
            rhs[1] += v * x * y;
            rhs[2] += v * y * y;
            rhs[3] += v * x;
            rhs[4] += v * y;
            rhs[5] += v;

            m[{0, 0}] += d * d * x * x * x * x;
            m[{0, 1}] += d * d * x * x * x * y;
            m[{0, 2}] += d * d * x * x * y * y;
            m[{0, 3}] += d * d * x * x * x;
            m[{0, 4}] += d * d * x * x * y;
            m[{0, 5}] += d * d * x * x;

            m[{1, 0}] += d * d * x * x * x * y;
            m[{1, 1}] += d * d * x * x * y * y;
            m[{1, 2}] += d * d * x * y * y * y;
            m[{1, 3}] += d * d * x * x * y;
            m[{1, 4}] += d * d * x * y * y;
            m[{1, 5}] += d * d * x * y;

            m[{2, 0}] += d * d * x * x * y * y;
            m[{2, 1}] += d * d * x * y * y * y;
            m[{2, 2}] += d * d * y * y * y * y;
            m[{2, 3}] += d * d * x * y * y;
            m[{2, 4}] += d * d * y * y * y;
            m[{2, 5}] += d * d * y * y;

            m[{3, 0}] += d * d * x * x * x;
            m[{3, 1}] += d * d * x * x * y;
            m[{3, 2}] += d * d * x * y * y;
            m[{3, 3}] += d * d * x * x;
            m[{3, 4}] += d * d * x * y;
            m[{3, 5}] += d * d * x;

            m[{4, 0}] += d * d * x * x * y;
            m[{4, 1}] += d * d * x * y * y;
            m[{4, 2}] += d * d * y * y * y;
            m[{4, 3}] += d * d * x * y;
            m[{4, 4}] += d * d * y * y;
            m[{4, 5}] += d * d * y;

            m[{5, 0}] += d * d * x * x;
            m[{5, 1}] += d * d * x * y;
            m[{5, 2}] += d * d * y * y;
            m[{5, 3}] += d * d * x;
            m[{5, 4}] += d * d * y;
            m[{5, 5}] += d * d;
        }
    }

    m[{0, 1}] *= static_cast<S>(2);
    m[{1, 1}] *= static_cast<S>(2);
    m[{2, 1}] *= static_cast<S>(2);
    m[{3, 1}] *= static_cast<S>(2);
    m[{4, 1}] *= static_cast<S>(2);
    m[{5, 1}] *= static_cast<S>(2);
}

template<class T>
bool extract_params(vec6_t<T> const& chi, T& scale, vec2<T>& mean, mat2s<T>& prec, T eps=math::num<T>::eps)
{
    prec.xx = -static_cast<T>(2) * chi[0];
    prec.xy = -static_cast<T>(2) * chi[1];
    prec.yy = -static_cast<T>(2) * chi[2];

    // mu = sigma * b = prec^-1 * B
    auto const d = det(prec);
    if (std::abs(d) <= eps) {
        return false;
    }

    mean.x = (prec.yy * chi[3] - prec.xy * chi[4]) / d;
    mean.y = (prec.xx * chi[4] - prec.xy * chi[3]) / d;

    scale = std::exp(chi[5] + xtmx(prec, mean) / static_cast<T>(2));

    return true;
}


template<class T>
inline void update_weight_maps(std::vector<parameters<T>>& params, image<T>& total)
{
    auto const scale = vec2<T> {
        static_cast<T>(2) * range<T>.x / static_cast<T>(total.shape().x),
        static_cast<T>(2) * range<T>.y / static_cast<T>(total.shape().y),
    };

    std::fill(total.begin(), total.end(), zero<T>());

    // compute individual Gaussians in sample windows
    for (auto& p : params) {
        if (!p.valid) {
            continue;
        }

        for (index_t iy = p.bounds.ymin; iy <= p.bounds.ymax; ++iy) {
            for (index_t ix = p.bounds.xmin; ix <= p.bounds.xmax; ++ix) {
                auto const x = static_cast<T>(ix) * scale.x - range<T>.x;
                auto const y = static_cast<T>(iy) * scale.y - range<T>.y;

                auto const v = p.scale * gaussian_like<T>({x, y}, p.mean, p.prec);

                p.weights[{ix - p.bounds.xmin, iy - p.bounds.ymin}] = v;
            }
        }
    }

    // sum up total
    for (auto& p : params) {
        if (!p.valid) {
            continue;
        }

        for (index_t y = p.bounds.ymin; y <= p.bounds.ymax; ++y) {
            for (index_t x = p.bounds.xmin; x <= p.bounds.xmax; ++x) {
                total[{x, y}] += p.weights[{x - p.bounds.xmin, y - p.bounds.ymin}];
            }
        }
    }

    // normalize weights
    for (auto& p : params) {
        if (!p.valid) {
            continue;
        }

        for (index_t y = p.bounds.ymin; y <= p.bounds.ymax; ++y) {
            for (index_t x = p.bounds.xmin; x <= p.bounds.xmax; ++x) {
                if (total[{x, y}] > static_cast<T>(0)) {
                    p.weights[{x - p.bounds.xmin, y - p.bounds.ymin}] /= total[{x, y}];
                }
            }
        }
    }
}

} /* namespace impl */


// TODO: vector as parameter container is not good... drops image memory when resized

template<class T>
void reserve(std::vector<parameters<T>>& params, std::size_t n, index2_t shape)
{
    if (n > params.size()) {
        params.resize(n, parameters<T> {
            false,
            static_cast<T>(1),
            { static_cast<T>(0), static_cast<T>(0) },
            { static_cast<T>(1), static_cast<T>(0), static_cast<T>(1) },
            { 0, -1, 0, -1 },
            image<T> { shape },
        });
    }

    for (auto& p : params) {
        p.valid = false;
    }
}

template<class T, class S>
void fit(std::vector<parameters<S>>& params, image<T> const& data, image<S>& tmp,
         unsigned int n_iter, S eps=math::num<S>::eps)
{
    auto const scale = vec2<S> {
        static_cast<S>(2) * range<S>.x / static_cast<S>(data.shape().x),
        static_cast<S>(2) * range<S>.y / static_cast<S>(data.shape().y),
    };

    // down-scaling
    for (auto& p : params) {
        if (!p.valid) {
            continue;
        }

        // scale and center mean
        p.mean.x = p.mean.x * scale.x - range<S>.x;
        p.mean.y = p.mean.y * scale.y - range<S>.y;

        // scale precision matrix (compute (S * Sigma * S^T)^-1 = S^-T * Prec * S^-1)
        p.prec.xx = p.prec.xx / (scale.x * scale.x);
        p.prec.xy = p.prec.xy / (scale.x * scale.y);
        p.prec.yy = p.prec.yy / (scale.y * scale.y);
    }

    // perform iterations
    for (unsigned int i = 0; i < n_iter; ++i) {
        // update weights
        impl::update_weight_maps(params, tmp);

        // fit individual parameters
        for (auto& p : params) {
            auto sys = mat6_t<S>{};
            auto rhs = vec6_t<S>{};
            auto chi = vec6_t<S>{};

            if (!p.valid) {
                continue;
            }

            // assemble system of linear equations
            impl::assemble_system(sys, rhs, p.bounds, data, p.weights);

            // solve systems
            p.valid = math::ge_solve(sys, rhs, chi, eps);
            if (!p.valid) {
                std::cout << "warning: invalid equation system\n";
                continue;
            }

            // get parameters
            p.valid = impl::extract_params(chi, p.scale, p.mean, p.prec, eps);
            if (!p.valid) {
                std::cout << "warning: parameter extraction failed\n";
            }
        }
    }

    // undo down-scaling
    for (auto& p : params) {
        if (!p.valid) {
            continue;
        }

        // un-scale and re-adjust mean
        p.mean.x = (p.mean.x + range<S>.x) / scale.x;
        p.mean.y = (p.mean.y + range<S>.y) / scale.y;

        // un-scale precision matrix
        p.prec.xx = p.prec.xx * scale.x * scale.x;
        p.prec.xy = p.prec.xy * scale.x * scale.y;
        p.prec.yy = p.prec.yy * scale.y * scale.y;
    }
}

} /* namespace gfit */
