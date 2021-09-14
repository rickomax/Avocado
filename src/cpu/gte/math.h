#pragma once
#include <array>
#include <cstdint>
namespace gte {
using Matrix = std::array<std::array<int16_t, 3>, 3>;

template <typename X, typename Y = X, typename Z = X>
struct Vector {
    union {
        X x, r;
    };

    union {
        Y y, g;
    };

    union {
        Z z, b;
    };

    Vector() = default;
    Vector(X x) : x(x), y(x), z(x) {}
    Vector(X x, Y y, Z z) : x(x), y(y), z(z) {}

    template <class Archive>
    void serialize(Archive &ar) {
        ar(x, y, z);
    }
};

Vector<int16_t> toVector(int16_t ir[4]);

struct Color {
    int32_t r = 0;
    int32_t g = 0;
    int32_t b = 0;
};

int isin(int x);

int icos(int x);

Matrix getIdentity();

Matrix mulMatrix(Matrix &matrixA, Matrix &matrixB);

Matrix rotMatrix(Vector<int16_t> angles);

Matrix rotMatrixX(int16_t angle);

Matrix rotMatrixY(int16_t angle);

Matrix rotMatrixZ(int16_t angle);

Vector<int32_t> applyMatrix(Matrix &matrix, Vector<int32_t> &vector);

};  // namespace gte