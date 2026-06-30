#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// 基础类型别名（等同 UE5 的 FVector, FMatrix 等）
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;

// 常量
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = PI * 0.5f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;