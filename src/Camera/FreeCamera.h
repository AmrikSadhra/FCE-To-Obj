#pragma once

#include "BaseCamera.h"

namespace OpenNFS {
    class FreeCamera : public BaseCamera {
    public:
        explicit FreeCamera() = default;

        FreeCamera(const std::shared_ptr<GLFWwindow> &window, glm::vec3 initialPosition);

        void ComputeMatricesFromInputs(float deltaTime);
    };
}
