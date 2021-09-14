#include "freecam_controller.h"
#include <fmt/core.h>
#include <utils/free_cam.h>

#include "input/input_manager.h"
#include "utils/screenshot.h"

namespace peripherals {
void FreecamController::update(std::string path) {
    InputManager* inputManager = InputManager::getInstance();
    if (inputManager == nullptr) {
        return;
    }
    FreeCamera* free_camera = FreeCamera::getInstance();
    if (free_camera->enabled) {
         int32_t inputX = inputManager->getAnalog(path + "freecam_left").value - inputManager->getAnalog(path + "freecam_right").value;
         int32_t inputY = inputManager->getAnalog(path + "freecam_up").value - inputManager->getAnalog(path + "freecam_down").value;
         int32_t inputZ = -inputManager->getAnalog(path + "freecam_forward").value + inputManager->getAnalog(path + "freecam_backward").value;
         int32_t rotX = -inputManager->getAnalog(path + "freecam_look_left").value + inputManager->getAnalog(path + "freecam_look_right").value;
         int32_t rotY = inputManager->getAnalog(path + "freecam_look_up").value - inputManager->getAnalog(path + "freecam_look_down").value;
         int32_t rotZ = -inputManager->getAnalog(path + "freecam_look_forward").value + inputManager->getAnalog(path + "freecam_look_backward").value;
        free_camera->processInput(inputX, inputY, inputZ, rotX, rotY, rotZ);
    }
}
};  // namespace peripherals