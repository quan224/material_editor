#pragma once
#include "Core/Public/MathTypes.h"
#include "Core/Public/Logger.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <variant>

using ConstValue = std::variant<float, Vec2, Vec3, Vec4>;


class ConstFolding{
public:
    static std::optional<ConstValue> FoldBinary(const std::string& op, const ConstValue& a, const ConstValue& b){
        if (DimOf(a) > 1 && DimOf(b) > 1 && DimOf(a) != DimOf(b)){
            return std::nullopt;
        }
        int dim = std::max(DimOf(a), DimOf(b));
        float f_a[4], f_b[4], r[4];
        FillArray(a, dim, f_a);
        FillArray(b, dim, f_b);
        if (op == "/"){
            for(int i = 0; i<dim; i++){
                if (f_b[i] == 0) return std::nullopt;
            }
        }

        // 先选运算（把 op 字符串判定提到循环外），非法 op 直接放弃折叠
        float (*fn)(float, float);
        if (op == "+")      fn = [](float x, float y){ return x + y; };
        else if (op == "-") fn = [](float x, float y){ return x - y; };
        else if (op == "*") fn = [](float x, float y){ return x * y; };
        else if (op == "/") fn = [](float x, float y){ return x / y; };
        else return std::nullopt;

        for (int i = 0; i < dim; i++) r[i] = fn(f_a[i], f_b[i]);
        return MakeValue(r, dim);
    }

    static std::optional<ConstValue> FoldUnary(const std::string& op, const ConstValue& a){
        const int dim = DimOf(a);
        float r[4];
        FillArray(a, dim, r);

        // 先选运算（op 判定提循环外），非法 op 放弃折叠
        float (*fn)(float);
        if (op == "abs")      fn = [](float x){ return std::abs(x); };
        else if (op == "neg") fn = [](float x){ return -x; };
        else if (op == "sin") fn = [](float x){ return std::sin(x); };
        else if (op == "cos") fn = [](float x){ return std::cos(x); };
        else return std::nullopt;

        for (int i = 0; i < dim; i++) r[i] = fn(r[i]);
        return MakeValue(r, dim);
    }

    static bool IsScalarZero(const ConstValue& a){
        return std::holds_alternative<float>(a) && std::get<float>(a) == 0.0f;
    }

    static bool IsScalarOne(const ConstValue& a){
        return std::holds_alternative<float>(a) && std::get<float>(a) == 1.0f;
    }


private:
    static int DimOf(const ConstValue& v){
        switch(v.index()){
            case 0: return 1;
            case 1: return 2;
            case 2: return 3;
            case 3: return 4;
            default:
                assert(false && "DimOf: invalid variant index");  // ConstValue 只有 4 种，到不了
                return 0;
        }
    }

    static void FillArray(const ConstValue& v, int dim, float f[4]){
        if (std::holds_alternative<float>(v)){
            float _v = std::get<float>(v);
            for(int i = 0; i<dim; i++) f[i] = _v;
        }
        else if(std::holds_alternative<Vec2>(v)){
            Vec2 _v = std::get<Vec2>(v);
            f[0] = _v.x; f[1] = _v.y;
        }
        else if(std::holds_alternative<Vec3>(v)){
            Vec3 _v = std::get<Vec3>(v);
            f[0] = _v.x; f[1] = _v.y; f[2] = _v.z;
        }
        else if(std::holds_alternative<Vec4>(v)){
            Vec4 _v = std::get<Vec4>(v);
            f[0] = _v.x; f[1] = _v.y; f[2] = _v.z; f[3] = _v.w;
        }
    }

    static ConstValue MakeValue(float f[4], int dim){
        switch(dim){
            case 1: return f[0];
            case 2: return Vec2(f[0], f[1]);
            case 3: return Vec3(f[0], f[1], f[2]);
            case 4: return Vec4(f[0], f[1], f[2], f[3]);
            default:
                ME_LOG_ERROR("MakeValue: invalid dim (must be 1-4) now: %d", dim);   // 先记日志（Debug/Release 都会打）
                assert(false && "MakeValue: invalid dim (must be 1-4)");             // 再崩（仅 Debug）
                return f[0];   // 仅为满足返回；release 下 assert 没了才走到（前提保证走不到）
        }
    }



};