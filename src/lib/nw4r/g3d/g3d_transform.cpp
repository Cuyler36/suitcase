#include "nw4r/g3d.h" // IWYU pragma: export

#include "nw4r/math.h" // IWYU pragma: export

namespace nw4r {
namespace g3d {
namespace detail {

using nw4r::math::MTX33;
using nw4r::math::MTX34;
using nw4r::math::VEC3;

const f32 det_epsilon = 1e-36f;

#if TARGET_PC
namespace {

// The 3x3 cofactors and determinant shared by the three functions below, in the order and with the
// operand order of the paired-single code (ps0/ps1 lanes are named _0/_1).
struct Cofactors {
    f32 t13_0, t13_1; // m11*m22 - m21*m12, m12*m20 - m22*m10
    f32 t12_0, t12_1; // m21*m02 - m01*m22, m22*m00 - m02*m20
    f32 t11_0, t11_1; // m01*m12 - m11*m02, m02*m10 - m12*m00
    f32 t10, t9, t8;  // m10*m21 - m11*m20, m01*m20 - m00*m21, m00*m11 - m01*m10
    f32 det;
};

Cofactors CalcCofactors(const MTX34 *pW) {
    Cofactors c;
    c.t11_0 = pW->_01 * pW->_12 - pW->_11 * pW->_02;
    c.t11_1 = pW->_02 * pW->_10 - pW->_12 * pW->_00;
    c.t13_0 = pW->_11 * pW->_22 - pW->_21 * pW->_12;
    c.t13_1 = pW->_12 * pW->_20 - pW->_22 * pW->_10;
    c.t12_0 = pW->_21 * pW->_02 - pW->_01 * pW->_22;
    c.t12_1 = pW->_22 * pW->_00 - pW->_02 * pW->_20;
    c.det = pW->_20 * c.t11_0 + (pW->_10 * c.t12_0 + pW->_00 * c.t13_0);
    c.t10 = pW->_10 * pW->_21 - pW->_11 * pW->_20;
    c.t9 = pW->_01 * pW->_20 - pW->_00 * pW->_21;
    c.t8 = pW->_00 * pW->_11 - pW->_01 * pW->_10;
    return c;
}

// ps_cmpo0 + bge: a NaN determinant takes the invert path, as on hardware
bool IsSingular(f32 det) {
    return std::fabs(det) < det_epsilon;
}

} // namespace

// Inverse-transpose of the upper 3x3 of pW
bool CalcViewNrmMtx(math::MTX33 *pOut, const math::MTX34 *pW) {
    Cofactors c = CalcCofactors(pW);
    if (IsSingular(c.det)) {
        MTX33 copy;
        copy._00 = pW->_00; copy._01 = pW->_01; copy._02 = pW->_02;
        copy._10 = pW->_10; copy._11 = pW->_11; copy._12 = pW->_12;
        copy._20 = pW->_20; copy._21 = pW->_21; copy._22 = pW->_22;
        *pOut = copy;
        return false;
    }

    f32 inv = 1.0f / c.det; // fres refined by one Newton step
    pOut->_00 = c.t13_0 * inv;
    pOut->_01 = c.t13_1 * inv;
    pOut->_10 = c.t12_0 * inv;
    pOut->_11 = c.t12_1 * inv;
    pOut->_20 = c.t11_0 * inv;
    pOut->_21 = c.t11_1 * inv;
    pOut->_02 = c.t10 * inv;
    pOut->_12 = c.t9 * inv;
    pOut->_22 = c.t8 * inv;
    return true;
}

// As CalcViewNrmMtx, into a 3x4 matrix with a zero translation
bool CalcViewTexMtx(math::MTX34 *pOut, const math::MTX34 *pW) {
    Cofactors c = CalcCofactors(pW);
    if (IsSingular(c.det)) {
        if (pOut != pW) {
            pOut->_00 = pW->_00; pOut->_01 = pW->_01; pOut->_02 = pW->_02; pOut->_03 = 0.0f;
            pOut->_10 = pW->_10; pOut->_11 = pW->_11; pOut->_12 = pW->_12; pOut->_13 = 0.0f;
            pOut->_20 = pW->_20; pOut->_21 = pW->_21; pOut->_22 = pW->_22; pOut->_23 = 0.0f;
        }
        return false;
    }

    f32 inv = 1.0f / c.det;
    pOut->_03 = 0.0f;
    pOut->_13 = 0.0f;
    pOut->_23 = 0.0f;
    pOut->_00 = c.t13_0 * inv;
    pOut->_01 = c.t13_1 * inv;
    pOut->_10 = c.t12_0 * inv;
    pOut->_11 = c.t12_1 * inv;
    pOut->_20 = c.t11_0 * inv;
    pOut->_21 = c.t11_1 * inv;
    pOut->_02 = c.t10 * inv;
    pOut->_12 = c.t9 * inv;
    pOut->_22 = c.t8 * inv;
    return true;
}

// Inverse of the affine matrix pW; pOut is left untouched if pW is singular
bool CalcInvWorldMtx(math::MTX34 *pOut, const math::MTX34 *pW) {
    Cofactors c = CalcCofactors(pW);
    if (IsSingular(c.det)) {
        return false;
    }

    f32 inv = 1.0f / c.det;
    f32 w03 = pW->_03;
    f32 w13 = pW->_13;
    f32 w23 = pW->_23;

    f32 i00 = c.t13_0 * inv, i01 = c.t12_0 * inv, i02 = c.t11_0 * inv;
    f32 i10 = c.t13_1 * inv, i11 = c.t12_1 * inv, i12 = c.t11_1 * inv;
    f32 i20 = c.t10 * inv, i21 = c.t9 * inv, i22 = c.t8 * inv;

    pOut->_00 = i00; pOut->_01 = i01; pOut->_02 = i02;
    pOut->_03 = -(i02 * w23 + (i01 * w13 + i00 * w03));
    pOut->_10 = i10; pOut->_11 = i11; pOut->_12 = i12;
    pOut->_13 = -(i12 * w23 + (i11 * w13 + i10 * w03));
    pOut->_20 = i20; pOut->_21 = i21; pOut->_22 = i22;
    pOut->_23 = -(i22 * w23 + (i21 * w13 + i20 * w03));
    return true;
}
#else
// clang-format off
asm bool CalcViewNrmMtx(register math::MTX33 *pOut, const register math::MTX34 *pW) {
    nofralloc
    psq_l      f0,  MTX34._00(pW), 1, 0
    psq_l      f1,  MTX34._01(pW), 0, 0
    psq_l      f2,  MTX34._10(pW), 1, 0
    ps_merge10 f6,  f1,  f0
    psq_l      f3,  MTX34._11(pW), 0, 0
    psq_l      f4,  MTX34._20(pW), 1, 0
    ps_merge10 f7,  f3,  f2
    psq_l      f5,  MTX34._21(pW), 0, 0
    ps_mul     f11, f3,  f6
    ps_merge10 f8,  f5,  f4
    ps_mul     f13, f5,  f7
    ps_msub    f11, f1,  f7,  f11
    ps_mul     f12, f1,  f8
    ps_msub    f13, f3,  f8,  f13
    lis        r12, det_epsilon@ha
    ps_msub    f12, f5,  f6,  f12
    ps_mul     f7,  f0,  f13
    addi       r12, r12, det_epsilon@l
    ps_mul     f10, f3,  f4
    ps_madd    f7,  f2,  f12, f7
    psq_l      f6,  0x0(r12), 1, 0
    ps_mul     f9,  f0,  f5
    ps_madd    f7,  f4,  f11, f7
    ps_msub    f10, f2,  f5,  f10
    ps_msub    f9,  f1,  f4,  f9
    ps_abs     f8,  f7
    ps_cmpo0   cr0, f8,  f6
    bge        _lbl_80442B80
    psq_st     f0,  MTX33._00(pOut), 1, 0
    psq_st     f2,  MTX33._10(pOut), 1, 0
    psq_st     f4,  MTX33._20(pOut), 1, 0
    psq_st     f1,  MTX33._01(pOut), 0, 0
    psq_st     f3,  MTX33._11(pOut), 0, 0
    psq_st     f5,  MTX33._21(pOut), 0, 0
    li         r3,  0x0
    blr

_lbl_80442B80:
    fres       f5,  f7
    ps_mul     f8,  f1,  f2
    ps_sub     f6,  f6,  f6
    ps_add     f1,  f5,  f5
    ps_msub    f8,  f0,  f3,  f8
    ps_mul     f2,  f7,  f5
    ps_nmsub   f0,  f5,  f2,  f1
    ps_muls0   f13, f13, f0
    ps_muls0   f12, f12, f0
    psq_st     f13, MTX33._00(pOut), 0, 0
    ps_muls0   f11, f11, f0
    psq_st     f12, MTX33._10(pOut), 0, 0
    ps_muls0   f10, f10, f0
    psq_st     f11, MTX33._20(pOut), 0, 0
    ps_muls0   f9,  f9,  f0
    psq_st     f10, MTX33._02(pOut), 1, 0
    ps_muls0   f8,  f8,  f0
    psq_st     f9,  MTX33._12(pOut), 1, 0
    psq_st     f8,  MTX33._22(pOut), 1, 0
    li         r3,  0x1
    blr
}

asm bool CalcViewTexMtx(register math::MTX34 *pOut, const register math::MTX34 *pW) {
    nofralloc
    psq_l      f0,  MTX34._00(pW), 1, 0
    psq_l      f1,  MTX34._01(pW), 0, 0
    psq_l      f2,  MTX34._10(pW), 1, 0
    ps_merge10 f6,  f1,  f0
    psq_l      f3,  MTX34._11(pW), 0, 0
    psq_l      f4,  MTX34._20(pW), 1, 0
    ps_merge10 f7,  f3,  f2
    psq_l      f5,  MTX34._21(pW), 0, 0
    ps_mul     f11, f3,  f6
    ps_merge10 f8,  f5,  f4
    ps_mul     f13, f5,  f7
    ps_msub    f11, f1,  f7,  f11
    ps_mul     f12, f1,  f8
    ps_msub    f13, f3,  f8,  f13
    lis        r12, det_epsilon@ha
    ps_msub    f12, f5,  f6,  f12
    ps_mul     f7,  f0,  f13
    addi       r12, r12, det_epsilon@l
    ps_mul     f10, f3,  f4
    ps_madd    f7,  f2,  f12, f7
    psq_l      f6,  0x0(r12), 1, 0
    ps_mul     f9,  f0,  f5
    ps_madd    f7,  f4,  f11, f7
    ps_msub    f10, f2,  f5,  f10
    ps_msub    f9,  f1,  f4,  f9
    ps_abs     f8,  f7
    ps_cmpo0   cr0, f8,  f6
    bge        _lbl_80442C88
    cmplw      pOut,  pW
    beq        _lbl_80442C80
    psq_st     f0, MTX34._00(pOut), 1, 0
    psq_st     f2, MTX34._10(pOut), 1, 0
    psq_st     f4, MTX34._20(pOut), 1, 0
    ps_sub     f6,  f6,  f6
    psq_st     f1, MTX34._01(pOut), 0, 0
    psq_st     f3, MTX34._11(pOut), 0, 0
    psq_st     f5, MTX34._21(pOut), 0, 0
    psq_st     f6, MTX34._03(pOut), 1, 0
    psq_st     f6, MTX34._13(pOut), 1, 0
    psq_st     f6, MTX34._23(pOut), 1, 0
_lbl_80442C80:
    li         r3, 0x0
    blr
_lbl_80442C88:
    fres       f5,  f7
    ps_mul     f8,  f1,  f2
    ps_sub     f6,  f6,  f6
    ps_add     f1,  f5,  f5
    ps_msub    f8,  f0,  f3,  f8
    psq_st     f6,  MTX34._03(pOut), 1, 0
    ps_mul     f2,  f7,  f5
    psq_st     f6,  MTX34._13(pOut), 1, 0
    psq_st     f6,  MTX34._23(pOut), 1, 0
    ps_nmsub   f0,  f5,  f2,  f1
    ps_muls0   f13, f13, f0
    ps_muls0   f12, f12, f0
    psq_st     f13, MTX34._00(pOut), 0, 0
    ps_muls0   f11, f11, f0
    psq_st     f12, MTX34._10(pOut), 0, 0
    ps_muls0   f10, f10, f0
    psq_st     f11, MTX34._20(pOut), 0, 0
    ps_muls0   f9,  f9,  f0
    psq_st     f10, MTX34._02(pOut), 1, 0
    ps_muls0   f8,  f8,  f0
    psq_st     f9,  MTX34._12(pOut), 1, 0
    psq_st     f8,  MTX34._22(pOut), 1, 0
    li         r3,  0x1
    blr
}

asm bool CalcInvWorldMtx(register math::MTX34 *pOut, const register math::MTX34 *pW) {
    nofralloc
    psq_l      f0, MTX34._00(pW), 1, 0
    psq_l      f1, MTX34._01(pW), 0, 0
    psq_l      f2, MTX34._10(pW), 1, 0
    ps_merge10 f6,  f1,  f0
    psq_l      f3, MTX34._11(pW), 0, 0
    psq_l      f4, MTX34._20(pW), 1, 0
    ps_merge10 f7,  f3,  f2
    psq_l      f5, MTX34._21(pW), 0, 0
    ps_mul     f11, f3,  f6
    ps_merge10 f8,  f5,  f4
    ps_mul     f13, f5,  f7
    ps_msub    f11, f1,  f7,  f11
    ps_mul     f12, f1,  f8
    ps_msub    f13, f3,  f8,  f13
    lis        r12, det_epsilon@ha
    ps_msub    f12, f5,  f6,  f12
    ps_mul     f7,  f0,  f13
    addi       r12, r12, det_epsilon@l
    ps_mul     f10, f3,  f4
    ps_madd    f7,  f2,  f12, f7
    psq_l      f6, 0x0(r12), 1, 0
    ps_mul     f9,  f0,  f5
    ps_madd    f7,  f4,  f11, f7
    ps_msub    f10, f2,  f5,  f10
    ps_msub    f9,  f1,  f4,  f9
    ps_abs     f8,  f7
    ps_cmpo0   cr0, f8,  f6
    bge        _lbl_80442D68
    li         r3, 0x0
    blr
_lbl_80442D68:
    fres       f6,  f7
    ps_mul     f8,  f1,  f2
    ps_add     f5,  f6,  f6
    ps_mul     f4,  f7,  f6
    ps_msub    f8,  f0,  f3,  f8
    ps_nmsub   f0,  f6,  f4,  f5
    lfs        f1,  MTX34._03(pW)
    ps_muls0   f13, f13, f0
    lfs        f2,  MTX34._13(pW)
    ps_muls0   f12, f12, f0
    lfs        f3,  MTX34._23(pW)
    ps_muls0   f11, f11, f0
    ps_merge00 f5,  f13, f12
    ps_merge11 f4,  f13, f12
    ps_mul     f6,  f13, f1
    psq_st     f5,  MTX34._00(pOut), 0, 0
    psq_st     f4,  MTX34._10(pOut), 0, 0
    ps_muls0   f10, f10, f0
    ps_muls0   f9,  f9,  f0
    ps_madd    f6,  f12, f2,  f6
    psq_st     f10, MTX34._20(pOut), 1, 0
    ps_muls0   f8,  f8,  f0
    ps_nmadd   f6,  f11, f3,  f6
    psq_st     f9,  MTX34._21(pOut), 1, 0
    ps_mul     f7,  f10, f1
    ps_merge00 f5,  f11, f6
    psq_st     f8,  MTX34._22(pOut), 1, 0
    ps_madd    f7,  f9,  f2,  f7
    ps_merge11 f4,  f11, f6
    psq_st     f5,  MTX34._02(pOut), 0, 0
    ps_nmadd   f7,  f8,  f3,  f7
    psq_st     f4,  MTX34._12(pOut), 0, 0
    psq_st     f7,  MTX34._23(pOut), 1, 0
    li         r3,  0x1
    blr
}

// clang-format on
#endif

} // namespace detail
} // namespace g3d
} // namespace nw4r
