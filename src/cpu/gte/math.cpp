// Some code here is based on Lameguy64's libpsnoob and OpenDriverEngine
// https://github.com/Lameguy64/PSn00bSDK
// https://github.com/OpenDriver2/OpenDriverEngine/blob/97aaf17e77d9646fb6c413fdbed2e638eae50847/math/psx_matrix.cpp

#include "math.h"

#define ONE 4096
#define qN 10
#define qA 12
#define B 19900
#define C 3516

namespace gte {
Vector<int16_t> toVector(int16_t ir[4]) { return Vector<int16_t>(ir[1], ir[2], ir[3]); }

int isin(int x) {
    int c = x << (30 - qN);  // Semi-circle info into carry.
    x -= 1 << qN;        // sine -> cosine calc

    x = x << (31 - qN);  // Mask with PI
    x = x >> (31 - qN);  // Note: SIGNED shift! (to qN)

    x = x * x >> (2 * qN - 14);  // x=x^2 To Q14

    int y = B - (x * C >> 14);          // B - x^2*C
    y = (1 << qA) - (x * y >> 16);  // A - x^2*(B-x^2*C)

    return c >= 0 ? y : -y;
}

int icos(int x) { return isin(x + 1024); }

Matrix getIdentity() {
    Matrix result;
    result[0][0] = ONE;
    result[0][1] = 0;
    result[0][2] = 0;
    result[1][0] = 0;
    result[1][1] = ONE;
    result[1][2] = 0;
    result[2][0] = 0;
    result[2][1] = 0;
    result[2][2] = ONE;
    return result;
}

Matrix mulMatrix(Matrix&matrixA, Matrix&matrixB) {
    Matrix result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result[i][j] = 0;
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                result[i][j] += matrixB[k][j] * matrixA[i][k] >> 12;
            }
        }
    }
    return result;
}

Matrix rotMatrix(Vector<int16_t> angles) {
    Matrix rotX = rotMatrixX(angles.x);
    Matrix rotY = rotMatrixY(angles.y);
    Matrix rotZ = rotMatrixZ(angles.z);
    Matrix result = mulMatrix(rotX, rotY);
    result = mulMatrix(result, rotZ);
    return result;
}

Matrix rotMatrixX(int16_t angle) {
    Matrix result;
    int s = isin(angle);
    int c = icos(angle);
    result[0][0] = ONE;
    result[0][1] = 0;
    result[0][2] = 0;
    result[1][0] = 0;
    result[1][1] = c;
    result[1][2] = -s;
    result[2][0] = 0;
    result[2][1] = s;
    result[2][2] = c;
    return result;
}

Matrix rotMatrixY(int16_t angle) {
    Matrix result;
    int s = isin(angle);
    int c = icos(angle);
    result[0][0] = c;
    result[0][1] = 0;
    result[0][2] = s;
    result[1][0] = 0;
    result[1][1] = ONE;
    result[1][2] = 0;
    result[2][0] = -s;
    result[2][1] = 0;
    result[2][2] = c;
    return result;
}

Matrix rotMatrixZ(int16_t angle) {
    Matrix result;
    int s = isin(angle);
    int c = icos(angle);
    result[0][0] = c;
    result[0][1] = -s;
    result[0][2] = 0;
    result[1][0] = s;
    result[1][1] = c;
    result[1][2] = 0;
    result[2][0] = 0;
    result[2][1] = 0;
    result[2][2] = ONE;
    return result;
}

Vector<int32_t> applyMatrix(Matrix&matrix, Vector<int32_t>&vector) {
    Vector<int32_t> result;
    result.x = matrix[0][0] * vector.x + matrix[0][1] * vector.y + matrix[0][2] * vector.z >> 12;
    result.y = matrix[1][0] * vector.x + matrix[1][1] * vector.y + matrix[1][2] * vector.z >> 12;
    result.z = matrix[2][0] * vector.x + matrix[2][1] * vector.y + matrix[2][2] * vector.z >> 12;
    return result;
}
};  // namespace gte