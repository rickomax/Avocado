#pragma once
#include "cpu/gte/math.h"
#include <fmt/core.h>
#include <utils/event.h>

struct FreeCamera {
    static FreeCamera *getInstance() {
        static FreeCamera instance;
        return &instance;
    }

    bool enabled = false;

    //rotation values
    int rotationX = 0;
    int rotationY = 0;
    int rotationZ = 0;

    //translation values
    int translationX = 0;
    int translationY = 0;
    int translationZ = 0;

    //perspective plane distance value
    int translationH = 0;

    //base rotation values (used to pre-align the camera since many games have the camera pointing slightly down)
    int base_rotationX = 0;
    int base_rotationY = 0;
    int base_rotationZ = 0;

    //camera speed value
    int cameraSpeed = 20;

   private:
    static int16_t toAngle(int degrees) {
        degrees = (degrees % 360 + 360) % 360;
        return (degrees / 360.0f) * 4096;
    }

    gte::Matrix getInvRotation() {
        gte::Matrix baseRotation = gte::rotMatrix(gte::Vector<int16_t>(toAngle(-base_rotationX), toAngle(-base_rotationY), toAngle(-base_rotationZ)));
        gte::Matrix preRotation = gte::rotMatrix(gte::Vector<int16_t>(toAngle(-rotationX), toAngle(-rotationY), toAngle(-rotationZ)));
        gte::Matrix postRotation = gte::mulMatrix(baseRotation, preRotation);
        return postRotation;
    }

    gte::Matrix getRotation() {
        gte::Matrix baseRotation = gte::rotMatrix(gte::Vector<int16_t>(toAngle(base_rotationX), toAngle(base_rotationY), toAngle(base_rotationZ)));
        gte::Matrix preRotation = gte::rotMatrix(gte::Vector<int16_t>(toAngle(rotationX), toAngle(rotationY), toAngle(rotationZ)));
        gte::Matrix postRotation = gte::mulMatrix(preRotation, baseRotation);
        return postRotation;
    }

public:
    //Multiplies the existing gte matrix registers with the free camera matrix registers
    void processRotTrans(gte::Matrix &gteRotation, gte::Vector<int32_t> &gteTranslation, gte::Matrix *newGteRotation, gte::Vector<int32_t> *newGteTranslation) {
        gte::Matrix freeCamRotation = getRotation();
        *newGteRotation = gte::mulMatrix(freeCamRotation, gteRotation);
        gte::Vector<int32_t> freeCamTranslation = gte::Vector<int32_t>(gteTranslation.x - translationX, gteTranslation.y - translationY, gteTranslation.z - translationZ);
        *newGteTranslation = gte::applyMatrix(freeCamRotation, freeCamTranslation);
    }
    
    //Processes the input and updates the free camera rotation and translation vectors
    void processInput(int8_t x, int8_t y, int8_t z, int8_t lx, int8_t ly, int8_t lz) {
        rotationX = rotationX + ly * 2;
        rotationY = rotationY + lx * 2;
        rotationZ = rotationZ + lz * 2;
        gte::Matrix invertRotation = getInvRotation();
        gte::Vector<int32_t> v(x * cameraSpeed, y * cameraSpeed, z * cameraSpeed);
        gte::Vector<int32_t> tv = gte::applyMatrix(invertRotation, v);

        //toast(fmt::format("----------------\n{}\t{}\t{}\n{}\t{}\t{}\n{}\t{}\t{}", 
        //    invertRotation[0][0], invertRotation[0][1], invertRotation[0][2],
        //    invertRotation[1][0], invertRotation[1][1], invertRotation[1][2], 
        //    invertRotation[2][0], invertRotation[2][1], invertRotation[2][2]
        //));

        //toast(fmt::format("----------------\nRot:{}\t{}\t{}\nTrs:{}\t{}\t{}", 
        //    rotationX,rotationY, rotationZ,
        //    tv.x, tv.y, tv.z));

        translationX += tv.x;
        translationY += tv.y;
        translationZ += tv.z;
    }

    void saveBase() {
        base_rotationX = rotationX;
        base_rotationY = rotationY;
        base_rotationZ = rotationZ;
        rotationX = 0;
        rotationY = 0;
        rotationZ = 0;
    }
    
    void resetMatrices() {
        translationX = 0;
        translationY = 0;
        translationZ = 0;
        rotationX = 0;
        rotationY = 0;
        rotationZ = 0;
        base_rotationX = 0;
        base_rotationY = 0;
        base_rotationZ = 0;
        translationH = 0;
        base_rotationX = 0;
        base_rotationY = 0;
        base_rotationZ = 0;
    }
};
