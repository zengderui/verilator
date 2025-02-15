// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Peephole optimizations over DfgGraph
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2022 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
//
// A pattern-matching based optimizer for DfgGraph. This is in some aspects similar to V3Const, but
// more powerful in that it does not care about ordering combinational statement. This is also less
// broadly applicable than V3Const, as it does not apply to procedural statements with sequential
// execution semantics.
//
//*************************************************************************

#include "config_build.h"

#include "V3DfgPeephole.h"

#include "V3Ast.h"
#include "V3Dfg.h"
#include "V3DfgPasses.h"
#include "V3Stats.h"

#include <algorithm>
#include <cctype>

VL_DEFINE_DEBUG_FUNCTIONS;

V3DfgPeepholeContext::V3DfgPeepholeContext(const std::string& label)
    : m_label{label} {
    const auto checkEnabled = [this](VDfgPeepholePattern id) {
        string str{id.ascii()};
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {  //
            return c == '_' ? '-' : std::tolower(c);
        });
        m_enabled[id] = v3Global.opt.fDfgPeepholeEnabled(str);
    };
#define OPTIMIZATION_CHECK_ENABLED(id, name) checkEnabled(VDfgPeepholePattern::id);
    FOR_EACH_DFG_PEEPHOLE_OPTIMIZATION(OPTIMIZATION_CHECK_ENABLED)
#undef OPTIMIZATION_CHECK_ENABLED
}

V3DfgPeepholeContext::~V3DfgPeepholeContext() {
    const auto emitStat = [this](VDfgPeepholePattern id) {
        string str{id.ascii()};
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {  //
            return c == '_' ? ' ' : std::tolower(c);
        });
        V3Stats::addStat("Optimizations, DFG " + m_label + " Peephole, " + str, m_count[id]);
    };
#define OPTIMIZATION_EMIT_STATS(id, name) emitStat(VDfgPeepholePattern::id);
    FOR_EACH_DFG_PEEPHOLE_OPTIMIZATION(OPTIMIZATION_EMIT_STATS)
#undef OPTIMIZATION_EMIT_STATS
}

// clang-format off
template <typename T_Reduction>
struct ReductionToBitwiseImpl {};
template <> struct ReductionToBitwiseImpl<DfgRedAnd> { using type = DfgAnd; };
template <> struct ReductionToBitwiseImpl<DfgRedOr>  { using type = DfgOr;  };
template <> struct ReductionToBitwiseImpl<DfgRedXor> { using type = DfgXor; };
template <typename T_Reductoin>
using ReductionToBitwise = typename ReductionToBitwiseImpl<T_Reductoin>::type;

template <typename T_Bitwise>
struct BitwiseToReductionImpl {};
template <> struct BitwiseToReductionImpl<DfgAnd> { using type = DfgRedAnd; };
template <> struct BitwiseToReductionImpl<DfgOr>  { using type = DfgRedOr;  };
template <> struct BitwiseToReductionImpl<DfgXor> { using type = DfgRedXor; };
template <typename T_Reductoin>
using BitwiseToReduction = typename BitwiseToReductionImpl<T_Reductoin>::type;

namespace {
template<typename Vertex> void foldOp(V3Number& out, const V3Number& src);
template <> void foldOp<DfgCLog2>      (V3Number& out, const V3Number& src) { out.opCLog2(src); }
template <> void foldOp<DfgCountOnes>  (V3Number& out, const V3Number& src) { out.opCountOnes(src); }
template <> void foldOp<DfgExtend>     (V3Number& out, const V3Number& src) { out.opAssign(src); }
template <> void foldOp<DfgExtendS>    (V3Number& out, const V3Number& src) { out.opExtendS(src, src.width()); }
template <> void foldOp<DfgLogNot>     (V3Number& out, const V3Number& src) { out.opLogNot(src); }
template <> void foldOp<DfgNegate>     (V3Number& out, const V3Number& src) { out.opNegate(src); }
template <> void foldOp<DfgNot>        (V3Number& out, const V3Number& src) { out.opNot(src); }
template <> void foldOp<DfgOneHot>     (V3Number& out, const V3Number& src) { out.opOneHot(src); }
template <> void foldOp<DfgOneHot0>    (V3Number& out, const V3Number& src) { out.opOneHot0(src); }
template <> void foldOp<DfgRedAnd>     (V3Number& out, const V3Number& src) { out.opRedAnd(src); }
template <> void foldOp<DfgRedOr>      (V3Number& out, const V3Number& src) { out.opRedOr(src); }
template <> void foldOp<DfgRedXor>     (V3Number& out, const V3Number& src) { out.opRedXor(src); }

template<typename Vertex> void foldOp(V3Number& out, const V3Number& lhs, const V3Number& rhs);
template <> void foldOp<DfgAdd>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opAdd(lhs, rhs); }
template <> void foldOp<DfgAnd>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opAnd(lhs, rhs); }
template <> void foldOp<DfgConcat>     (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opConcat(lhs, rhs); }
template <> void foldOp<DfgDiv>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opDiv(lhs, rhs); }
template <> void foldOp<DfgDivS>       (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opDivS(lhs, rhs); }
template <> void foldOp<DfgEq>         (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opEq(lhs, rhs); }
template <> void foldOp<DfgGt>         (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opGt(lhs, rhs); }
template <> void foldOp<DfgGtS>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opGtS(lhs, rhs); }
template <> void foldOp<DfgGte>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opGte(lhs, rhs); }
template <> void foldOp<DfgGteS>       (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opGteS(lhs, rhs); }
template <> void foldOp<DfgLogAnd>     (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLogAnd(lhs, rhs); }
template <> void foldOp<DfgLogEq>      (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLogEq(lhs, rhs); }
template <> void foldOp<DfgLogIf>      (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLogIf(lhs, rhs); }
template <> void foldOp<DfgLogOr>      (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLogOr(lhs, rhs); }
template <> void foldOp<DfgLt>         (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLt(lhs, rhs); }
template <> void foldOp<DfgLtS>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLtS(lhs, rhs); }
template <> void foldOp<DfgLte>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLte(lhs, rhs); }
template <> void foldOp<DfgLteS>       (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opLtS(lhs, rhs); }
template <> void foldOp<DfgModDiv>     (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opModDiv(lhs, rhs); }
template <> void foldOp<DfgModDivS>    (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opModDivS(lhs, rhs); }
template <> void foldOp<DfgMul>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opMul(lhs, rhs); }
template <> void foldOp<DfgMulS>       (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opMulS(lhs, rhs); }
template <> void foldOp<DfgNeq>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opNeq(lhs, rhs); }
template <> void foldOp<DfgOr>         (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opOr(lhs, rhs); }
template <> void foldOp<DfgPow>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opPow(lhs, rhs); }
template <> void foldOp<DfgPowSS>      (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opPowSS(lhs, rhs); }
template <> void foldOp<DfgPowSU>      (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opPowSU(lhs, rhs); }
template <> void foldOp<DfgPowUS>      (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opPowUS(lhs, rhs); }
template <> void foldOp<DfgReplicate>  (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opRepl(lhs, rhs); }
template <> void foldOp<DfgShiftL>     (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opShiftL(lhs, rhs); }
template <> void foldOp<DfgShiftR>     (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opShiftR(lhs, rhs); }
template <> void foldOp<DfgShiftRS>    (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opShiftRS(lhs, rhs, lhs.width()); }
template <> void foldOp<DfgSub>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opSub(lhs, rhs); }
template <> void foldOp<DfgXor>        (V3Number& out, const V3Number& lhs, const V3Number& rhs) { out.opXor(lhs, rhs); }
}
// clang-format on

class V3DfgPeephole final : public DfgVisitor {

    // STATE
    DfgGraph& m_dfg;  // The DfgGraph being visited
    V3DfgPeepholeContext& m_ctx;  // The config structure
    bool m_changed = false;  // Changed a vertex
    AstNodeDType* const m_bitDType = DfgVertex::dtypeForWidth(1);  // Common, so grab it up front

#define APPLYING(id) if (checkApplying(VDfgPeepholePattern::id))

    // METHODS
    bool checkApplying(VDfgPeepholePattern id) {
        if (!m_ctx.m_enabled[id]) return false;
        UINFO(9, "Applying DFG patten " << id.ascii() << endl);
        ++m_ctx.m_count[id];
        m_changed = true;
        return true;
    }

    // Shorthand
    static AstNodeDType* dtypeForWidth(uint32_t width) { return DfgVertex::dtypeForWidth(width); }

    // Create a 32-bit DfgConst vertex
    DfgConst* makeI32(FileLine* flp, uint32_t val) { return new DfgConst{m_dfg, flp, 32, val}; }

    // Create a DfgConst vertex with the given width and value zero
    DfgConst* makeZero(FileLine* flp, uint32_t width) { return new DfgConst{m_dfg, flp, width}; }

    // Constant fold unary vertex, return true if folded
    template <typename Vertex>
    bool foldUnary(Vertex* vtxp) {
        static_assert(std::is_base_of<DfgVertexUnary, Vertex>::value, "Must invoke on unary");
        static_assert(std::is_final<Vertex>::value, "Must invoke on final class");
        if (DfgConst* const srcp = vtxp->srcp()->template cast<DfgConst>()) {
            APPLYING(FOLD_UNARY) {
                DfgConst* const resultp = makeZero(vtxp->fileline(), vtxp->width());
                foldOp<Vertex>(resultp->num(), srcp->num());
                vtxp->replaceWith(resultp);
                return true;
            }
        }
        return false;
    }

    // Constant fold binary vertex, return true if folded
    template <typename Vertex>
    bool foldBinary(Vertex* vtxp) {
        static_assert(std::is_base_of<DfgVertexBinary, Vertex>::value, "Must invoke on binary");
        static_assert(std::is_final<Vertex>::value, "Must invoke on final class");
        if (DfgConst* const lhsp = vtxp->lhsp()->template cast<DfgConst>()) {
            if (DfgConst* const rhsp = vtxp->rhsp()->template cast<DfgConst>()) {
                APPLYING(FOLD_BINARY) {
                    DfgConst* const resultp = makeZero(vtxp->fileline(), vtxp->width());
                    foldOp<Vertex>(resultp->num(), lhsp->num(), rhsp->num());
                    vtxp->replaceWith(resultp);
                    return true;
                }
            }
        }
        return false;
    }

    // Rotate the expression tree rooted at 'vtxp' to the right ('vtxp->lhsp()' becomes root,
    // producing a right-leaning tree). Warning: only valid for associative operations.
    template <typename Vertex>
    void rotateRight(Vertex* vtxp) {
        static_assert(std::is_base_of<DfgVertexBinary, Vertex>::value, "Must invoke on binary");
        static_assert(std::is_final<Vertex>::value, "Must invoke on final class");
        DfgVertexBinary* const ap = vtxp;
        DfgVertexBinary* const bp = vtxp->lhsp()->template as<Vertex>();
        UASSERT_OBJ(!bp->hasMultipleSinks(), vtxp, "Can't rotate a non-tree");
        ap->replaceWith(bp);
        ap->lhsp(bp->rhsp());
        bp->rhsp(ap);
        // Concatenation dtypes need to be fixed up, other associative nodes preserve types
        if VL_CONSTEXPR_CXX17 (std::is_same<DfgConcat, Vertex>::value) {
            ap->dtypep(dtypeForWidth(ap->lhsp()->width() + ap->rhsp()->width()));
            bp->dtypep(dtypeForWidth(bp->lhsp()->width() + bp->rhsp()->width()));
        }
    }

    // Transformations that apply to all associative binary vertices.
    // Returns true if vtxp was replaced.
    template <typename Vertex>
    bool associativeBinary(Vertex* vtxp) {
        static_assert(std::is_base_of<DfgVertexBinary, Vertex>::value, "Must invoke on binary");
        static_assert(std::is_final<Vertex>::value, "Must invoke on final class");

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();
        FileLine* const flp = vtxp->fileline();

        DfgConst* const lConstp = lhsp->cast<DfgConst>();
        DfgConst* const rConstp = rhsp->cast<DfgConst>();

        if (lConstp && rConstp) {
            APPLYING(FOLD_ASSOC_BINARY) {
                DfgConst* const resultp = makeZero(flp, vtxp->width());
                foldOp<Vertex>(resultp->num(), lConstp->num(), rConstp->num());
                vtxp->replaceWith(resultp);
                return true;
            }
        }

        if (lConstp) {
            if (Vertex* const rVtxp = rhsp->cast<Vertex>()) {
                if (DfgConst* const rlConstp = rVtxp->lhsp()->template cast<DfgConst>()) {
                    APPLYING(FOLD_ASSOC_BINARY_LHS_OF_RHS) {
                        // Fold constants
                        const uint32_t width = std::is_same<DfgConcat, Vertex>::value
                                                   ? lConstp->width() + rlConstp->width()
                                                   : vtxp->width();
                        DfgConst* const constp = makeZero(flp, width);
                        foldOp<Vertex>(constp->num(), lConstp->num(), rlConstp->num());

                        // Replace vertex
                        if VL_CONSTEXPR_CXX17 (!std::is_same<DfgConcat, Vertex>::value) {
                            rVtxp->lhsp(constp);
                            vtxp->replaceWith(rVtxp);
                        } else if (!rVtxp->hasMultipleSinks()) {
                            rVtxp->lhsp(constp);
                            rVtxp->dtypep(vtxp->dtypep());
                            vtxp->replaceWith(rVtxp);
                        } else {
                            Vertex* const resp = new Vertex{m_dfg, flp, vtxp->dtypep()};
                            resp->lhsp(constp);
                            resp->rhsp(rVtxp->rhsp());
                            vtxp->replaceWith(resp);
                        }
                        return true;
                    }
                }
            }
        }

        if (rConstp) {
            if (Vertex* const lVtxp = lhsp->cast<Vertex>()) {
                if (DfgConst* const lrConstp = lVtxp->rhsp()->template cast<DfgConst>()) {
                    APPLYING(FOLD_ASSOC_BINARY_RHS_OF_LHS) {
                        // Fold constants
                        const uint32_t width = std::is_same<DfgConcat, Vertex>::value
                                                   ? lrConstp->width() + rConstp->width()
                                                   : vtxp->width();
                        DfgConst* const constp = makeZero(flp, width);
                        foldOp<Vertex>(constp->num(), lrConstp->num(), rConstp->num());

                        // Replace vertex
                        if VL_CONSTEXPR_CXX17 (!std::is_same<DfgConcat, Vertex>::value) {
                            lVtxp->rhsp(constp);
                            vtxp->replaceWith(lVtxp);
                        } else if (!lVtxp->hasMultipleSinks()) {
                            lVtxp->rhsp(constp);
                            lVtxp->dtypep(vtxp->dtypep());
                            vtxp->replaceWith(lVtxp);
                        } else {
                            Vertex* const resp = new Vertex{m_dfg, flp, vtxp->dtypep()};
                            resp->lhsp(lVtxp->lhsp());
                            resp->rhsp(constp);
                            vtxp->replaceWith(resp);
                        }
                        return true;
                    }
                }
            }
        }

        // Make associative trees right leaning to reduce pattern variations, and for better CSE
        while (vtxp->lhsp()->template is<Vertex>() && !vtxp->lhsp()->hasMultipleSinks()) {
            APPLYING(RIGHT_LEANING_ASSOC) {
                rotateRight(vtxp);
                continue;
            }
            break;
        }

        return false;
    }

    // Transformations that apply to all commutative binary vertices
    void commutativeBinary(DfgVertexBinary* vtxp) {
        DfgVertex* const lhsp = vtxp->source<0>();
        DfgVertex* const rhsp = vtxp->source<1>();
        // Ensure Const is on left-hand side to simplify other patterns
        if (lhsp->is<DfgConst>()) return;
        if (rhsp->is<DfgConst>()) {
            APPLYING(SWAP_CONST_IN_COMMUTATIVE_BINARY) {
                vtxp->lhsp(rhsp);
                vtxp->rhsp(lhsp);
                return;
            }
        }
        // Ensure Not is on the left-hand side to simplify other patterns
        if (lhsp->is<DfgNot>()) return;
        if (rhsp->is<DfgNot>()) {
            APPLYING(SWAP_NOT_IN_COMMUTATIVE_BINARY) {
                vtxp->lhsp(rhsp);
                vtxp->rhsp(lhsp);
                return;
            }
        }
        // If both sides are variable references, order the side in some defined way. This allows
        // CSE to later merge 'a op b' with 'b op a'.
        if (lhsp->is<DfgVertexVar>() && rhsp->is<DfgVertexVar>()) {
            AstVar* const lVarp = lhsp->as<DfgVertexVar>()->varp();
            AstVar* const rVarp = rhsp->as<DfgVertexVar>()->varp();
            if (lVarp->name() > rVarp->name()) {
                APPLYING(SWAP_VAR_IN_COMMUTATIVE_BINARY) {
                    vtxp->lhsp(rhsp);
                    vtxp->rhsp(lhsp);
                    return;
                }
            }
        }
    }

    // Bitwise operation with one side Const, and the other side a Concat
    template <typename Vertex>
    bool tryPushBitwiseOpThroughConcat(Vertex* vtxp, DfgConst* constp, DfgConcat* concatp) {
        UASSERT_OBJ(constp->dtypep() == concatp->dtypep(), vtxp, "Mismatched widths");

        FileLine* const flp = vtxp->fileline();

        // If at least one of the sides of the Concat constant, or width 1 (i.e.: can be
        // further simplified), then push the Vertex past the Concat
        if (concatp->lhsp()->is<DfgConst>() || concatp->rhsp()->is<DfgConst>()  //
            || concatp->lhsp()->dtypep() == m_bitDType
            || concatp->rhsp()->dtypep() == m_bitDType) {
            APPLYING(PUSH_BITWISE_OP_THROUGH_CONCAT) {
                const uint32_t width = concatp->width();
                AstNodeDType* const lDtypep = concatp->lhsp()->dtypep();
                AstNodeDType* const rDtypep = concatp->rhsp()->dtypep();
                const uint32_t lWidth = lDtypep->width();
                const uint32_t rWidth = rDtypep->width();

                // The new Lhs vertex
                Vertex* const newLhsp = new Vertex{m_dfg, flp, lDtypep};
                DfgConst* const newLhsConstp = makeZero(constp->fileline(), lWidth);
                newLhsConstp->num().opSel(constp->num(), width - 1, rWidth);
                newLhsp->lhsp(newLhsConstp);
                newLhsp->rhsp(concatp->lhsp());

                // The new Rhs vertex
                Vertex* const newRhsp = new Vertex{m_dfg, flp, rDtypep};
                DfgConst* const newRhsConstp = makeZero(constp->fileline(), rWidth);
                newRhsConstp->num().opSel(constp->num(), rWidth - 1, 0);
                newRhsp->lhsp(newRhsConstp);
                newRhsp->rhsp(concatp->rhsp());

                // The replacement Concat vertex
                DfgConcat* const newConcat
                    = new DfgConcat{m_dfg, concatp->fileline(), concatp->dtypep()};
                newConcat->lhsp(newLhsp);
                newConcat->rhsp(newRhsp);

                // Replace this vertex
                vtxp->replaceWith(newConcat);
                return true;
            }
        }
        return false;
    }

    template <typename Vertex>
    bool tryPushCompareOpThroughConcat(Vertex* vtxp, DfgConst* constp, DfgConcat* concatp) {
        UASSERT_OBJ(constp->dtypep() == concatp->dtypep(), vtxp, "Mismatched widths");

        FileLine* const flp = vtxp->fileline();

        // If at least one of the sides of the Concat is constant, then push the Vertex past the
        // Concat
        if (concatp->lhsp()->is<DfgConst>() || concatp->rhsp()->is<DfgConst>()) {
            APPLYING(PUSH_COMPARE_OP_THROUGH_CONCAT) {
                const uint32_t width = concatp->width();
                const uint32_t lWidth = concatp->lhsp()->width();
                const uint32_t rWidth = concatp->rhsp()->width();

                // The new Lhs vertex
                Vertex* const newLhsp = new Vertex{m_dfg, flp, m_bitDType};
                DfgConst* const newLhsConstp = makeZero(constp->fileline(), lWidth);
                newLhsConstp->num().opSel(constp->num(), width - 1, rWidth);
                newLhsp->lhsp(newLhsConstp);
                newLhsp->rhsp(concatp->lhsp());

                // The new Rhs vertex
                Vertex* const newRhsp = new Vertex{m_dfg, flp, m_bitDType};
                DfgConst* const newRhsConstp = makeZero(constp->fileline(), rWidth);
                newRhsConstp->num().opSel(constp->num(), rWidth - 1, 0);
                newRhsp->lhsp(newRhsConstp);
                newRhsp->rhsp(concatp->rhsp());

                // The replacement Vertex
                DfgVertexBinary* const replacementp
                    = std::is_same<Vertex, DfgEq>::value
                          ? new DfgAnd{m_dfg, concatp->fileline(), m_bitDType}
                          : nullptr;
                UASSERT_OBJ(replacementp, vtxp,
                            "Unhandled vertex type in 'tryPushCompareOpThroughConcat': "
                                << vtxp->typeName());
                replacementp->relinkSource<0>(newLhsp);
                replacementp->relinkSource<1>(newRhsp);

                // Replace this vertex
                vtxp->replaceWith(replacementp);
                return true;
            }
        }
        return false;
    }

    template <typename Bitwise>
    bool tryPushBitwiseOpThroughReductions(Bitwise* vtxp) {
        using Reduction = BitwiseToReduction<Bitwise>;

        if (Reduction* const lRedp = vtxp->lhsp()->template cast<Reduction>()) {
            if (Reduction* const rRedp = vtxp->rhsp()->template cast<Reduction>()) {
                DfgVertex* const lSrcp = lRedp->srcp();
                DfgVertex* const rSrcp = rRedp->srcp();
                if (lSrcp->dtypep() == rSrcp->dtypep() && lSrcp->width() <= 64
                    && !lSrcp->hasMultipleSinks() && !rSrcp->hasMultipleSinks()) {
                    APPLYING(PUSH_BITWISE_THROUGH_REDUCTION) {
                        FileLine* const flp = vtxp->fileline();
                        Bitwise* const bwp = new Bitwise{m_dfg, flp, lSrcp->dtypep()};
                        bwp->lhsp(lSrcp);
                        bwp->rhsp(rSrcp);
                        Reduction* const redp = new Reduction{m_dfg, flp, m_bitDType};
                        redp->srcp(bwp);
                        vtxp->replaceWith(redp);
                        return true;
                    }
                }
            }
        }

        return false;
    }

    template <typename Reduction>
    void optimizeReduction(Reduction* vtxp) {
        using Bitwise = ReductionToBitwise<Reduction>;

        if (foldUnary(vtxp)) return;

        DfgVertex* const srcp = vtxp->srcp();
        FileLine* const flp = vtxp->fileline();

        // Reduction of 1-bit value
        if (srcp->dtypep() == m_bitDType) {
            APPLYING(REMOVE_WIDTH_ONE_REDUCTION) {
                vtxp->replaceWith(srcp);
                return;
            }
        }

        if (DfgCond* const condp = srcp->cast<DfgCond>()) {
            if (condp->thenp()->is<DfgConst>() || condp->elsep()->is<DfgConst>()) {
                APPLYING(PUSH_REDUCTION_THROUGH_COND_WITH_CONST_BRANCH) {
                    // The new 'then' vertex
                    Reduction* const newThenp = new Reduction{m_dfg, flp, m_bitDType};
                    newThenp->srcp(condp->thenp());

                    // The new 'else' vertex
                    Reduction* const newElsep = new Reduction{m_dfg, flp, m_bitDType};
                    newElsep->srcp(condp->elsep());

                    // The replacement Cond vertex
                    DfgCond* const newCondp = new DfgCond{m_dfg, condp->fileline(), m_bitDType};
                    newCondp->condp(condp->condp());
                    newCondp->thenp(newThenp);
                    newCondp->elsep(newElsep);

                    // Replace this vertex
                    vtxp->replaceWith(newCondp);
                    return;
                }
            }
        }

        if (DfgConcat* const concatp = srcp->cast<DfgConcat>()) {
            if (concatp->lhsp()->is<DfgConst>() || concatp->rhsp()->is<DfgConst>()) {
                APPLYING(PUSH_REDUCTION_THROUGH_CONCAT) {
                    // Reduce the parts of the concatenation
                    Reduction* const lRedp = new Reduction{m_dfg, concatp->fileline(), m_bitDType};
                    lRedp->srcp(concatp->lhsp());
                    Reduction* const rRedp = new Reduction{m_dfg, concatp->fileline(), m_bitDType};
                    rRedp->srcp(concatp->rhsp());

                    // Bitwise reduce the results
                    Bitwise* const replacementp = new Bitwise{m_dfg, flp, m_bitDType};
                    replacementp->lhsp(lRedp);
                    replacementp->rhsp(rRedp);
                    vtxp->replaceWith(replacementp);

                    // Optimize the new terms
                    optimizeReduction(lRedp);
                    optimizeReduction(rRedp);
                    iterate(replacementp);
                    return;
                }
            }
        }
    }

    void optimizeShiftRHS(DfgVertexBinary* vtxp) {
        if (const DfgConcat* const concatp = vtxp->rhsp()->cast<DfgConcat>()) {
            if (concatp->lhsp()->isZero()) {  // Drop redundant zero extension
                APPLYING(REMOVE_REDUNDANT_ZEXT_ON_RHS_OF_SHIFT) {  //
                    vtxp->rhsp(concatp->rhsp());
                }
            }
        }
    }

    // VISIT methods

    void visit(DfgVertex*) override {}

    //=========================================================================
    //  DfgVertexUnary
    //=========================================================================

    void visit(DfgCLog2* vtxp) override {
        if (foldUnary(vtxp)) return;
    }

    void visit(DfgCountOnes* vtxp) override {
        if (foldUnary(vtxp)) return;
    }

    void visit(DfgExtend* vtxp) override {
        UASSERT_OBJ(vtxp->width() > vtxp->srcp()->width(), vtxp, "Invalid zero extend");

        if (foldUnary(vtxp)) return;

        // Convert all Extend into Concat with zeros. This simplifies other patterns as they only
        // need to handle Concat, which is more generic, and don't need special cases for
        // Extend.
        APPLYING(REPLACE_EXTEND) {
            FileLine* const flp = vtxp->fileline();
            DfgConcat* const replacementp = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
            replacementp->lhsp(makeZero(flp, vtxp->width() - vtxp->srcp()->width()));
            replacementp->rhsp(vtxp->srcp());
            vtxp->replaceWith(replacementp);
        }
    }

    void visit(DfgExtendS* vtxp) override {
        UASSERT_OBJ(vtxp->width() > vtxp->srcp()->width(), vtxp, "Invalid sign extend");

        if (foldUnary(vtxp)) return;
    }

    void visit(DfgLogNot* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == m_bitDType, vtxp, "Incorrect width");

        if (foldUnary(vtxp)) return;
    }

    void visit(DfgNegate* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->srcp()->dtypep(), vtxp, "Mismatched width");

        if (foldUnary(vtxp)) return;
    }

    void visit(DfgNot* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->srcp()->dtypep(), vtxp, "Mismatched width");

        if (foldUnary(vtxp)) return;

        // Not of Cond
        if (DfgCond* const condp = vtxp->srcp()->cast<DfgCond>()) {
            // If at least one of the branches are a constant, push the Not past the Cond
            if (condp->thenp()->is<DfgConst>() || condp->elsep()->is<DfgConst>()) {
                APPLYING(PUSH_NOT_THROUGH_COND) {
                    // The new 'then' vertex
                    DfgNot* const newThenp = new DfgNot{m_dfg, vtxp->fileline(), vtxp->dtypep()};
                    newThenp->srcp(condp->thenp());

                    // The new 'else' vertex
                    DfgNot* const newElsep = new DfgNot{m_dfg, vtxp->fileline(), vtxp->dtypep()};
                    newElsep->srcp(condp->elsep());

                    // The replacement Cond vertex
                    DfgCond* const newCondp
                        = new DfgCond{m_dfg, condp->fileline(), vtxp->dtypep()};
                    newCondp->condp(condp->condp());
                    newCondp->thenp(newThenp);
                    newCondp->elsep(newElsep);

                    // Replace this vertex
                    vtxp->replaceWith(newCondp);
                    return;
                }
            }
        }

        // Not of Not
        if (DfgNot* const notp = vtxp->srcp()->cast<DfgNot>()) {
            UASSERT_OBJ(vtxp->dtypep() == notp->srcp()->dtypep(), vtxp, "Width mismatch");
            APPLYING(REMOVE_NOT_NOT) {
                vtxp->replaceWith(notp->srcp());
                return;
            }
        }

        if (!vtxp->srcp()->hasMultipleSinks()) {
            // Not of Eq
            if (DfgEq* const eqp = vtxp->srcp()->cast<DfgEq>()) {
                APPLYING(REPLACE_NOT_EQ) {
                    DfgNeq* const replacementp
                        = new DfgNeq{m_dfg, eqp->fileline(), vtxp->dtypep()};
                    replacementp->lhsp(eqp->lhsp());
                    replacementp->rhsp(eqp->rhsp());
                    vtxp->replaceWith(replacementp);
                    return;
                }
            }

            // Not of Neq
            if (DfgNeq* const neqp = vtxp->srcp()->cast<DfgNeq>()) {
                APPLYING(REPLACE_NOT_NEQ) {
                    DfgEq* const replacementp = new DfgEq{m_dfg, neqp->fileline(), vtxp->dtypep()};
                    replacementp->lhsp(neqp->lhsp());
                    replacementp->rhsp(neqp->rhsp());
                    vtxp->replaceWith(replacementp);
                    return;
                }
            }
        }
    }

    void visit(DfgOneHot* vtxp) override {
        if (foldUnary(vtxp)) return;
    }

    void visit(DfgOneHot0* vtxp) override {
        if (foldUnary(vtxp)) return;
    }

    void visit(DfgRedOr* vtxp) override { optimizeReduction(vtxp); }

    void visit(DfgRedAnd* vtxp) override { optimizeReduction(vtxp); }

    void visit(DfgRedXor* vtxp) override { optimizeReduction(vtxp); }

    void visit(DfgSel* vtxp) override {
        DfgVertex* const fromp = vtxp->fromp();

        FileLine* const flp = vtxp->fileline();

        const uint32_t lsb = vtxp->lsb();
        const uint32_t width = vtxp->width();
        const uint32_t msb = lsb + width - 1;

        if (DfgConst* const constp = fromp->cast<DfgConst>()) {
            APPLYING(FOLD_SEL) {
                DfgConst* const replacementp = makeZero(flp, width);
                replacementp->num().opSel(constp->num(), msb, lsb);
                vtxp->replaceWith(replacementp);
                return;
            }
        }

        // Full width select, replace with the source.
        if (fromp->width() == width) {
            UASSERT_OBJ(lsb == 0, fromp, "OOPS");
            APPLYING(REMOVE_FULL_WIDTH_SEL) {
                vtxp->replaceWith(fromp);
                return;
            }
        }

        // Sel from Concat
        if (DfgConcat* const concatp = fromp->cast<DfgConcat>()) {
            DfgVertex* const lhsp = concatp->lhsp();
            DfgVertex* const rhsp = concatp->rhsp();

            if (msb < rhsp->width()) {
                // If the select is entirely from rhs, then replace with sel from rhs
                APPLYING(REMOVE_SEL_FROM_RHS_OF_CONCAT) {  //
                    vtxp->fromp(rhsp);
                }
            } else if (lsb >= rhsp->width()) {
                // If the select is entirely from the lhs, then replace with sel from lhs
                APPLYING(REMOVE_SEL_FROM_LHS_OF_CONCAT) {
                    vtxp->fromp(lhsp);
                    vtxp->lsb(lsb - rhsp->width());
                }
            } else if (lsb == 0 || msb == concatp->width() - 1  //
                       || lhsp->is<DfgConst>() || rhsp->is<DfgConst>()  //
                       || !concatp->hasMultipleSinks()) {
                // If the select straddles both sides, but at least one of the sides is wholly
                // selected, or at least one of the sides is a Const, or this concat has no other
                // use, then push the Sel past the Concat
                APPLYING(PUSH_SEL_THROUGH_CONCAT) {
                    const uint32_t rSelWidth = rhsp->width() - lsb;
                    const uint32_t lSelWidth = width - rSelWidth;

                    // The new Lhs vertex
                    DfgSel* const newLhsp = new DfgSel{m_dfg, flp, dtypeForWidth(lSelWidth)};
                    newLhsp->fromp(lhsp);
                    newLhsp->lsb(0);

                    // The new Rhs vertex
                    DfgSel* const newRhsp = new DfgSel{m_dfg, flp, dtypeForWidth(rSelWidth)};
                    newRhsp->fromp(rhsp);
                    newRhsp->lsb(lsb);

                    // The replacement Concat vertex
                    DfgConcat* const newConcat
                        = new DfgConcat{m_dfg, concatp->fileline(), vtxp->dtypep()};
                    newConcat->lhsp(newLhsp);
                    newConcat->rhsp(newRhsp);

                    // Replace this vertex
                    vtxp->replaceWith(newConcat);
                    return;
                }
            }
        }

        if (DfgReplicate* const repp = fromp->cast<DfgReplicate>()) {
            // If the Sel is wholly into the source of the Replicate, push the Sel through the
            // Replicate and apply it directly to the source of the Replicate.
            const uint32_t srcWidth = repp->srcp()->width();
            if (width <= srcWidth) {
                const uint32_t newLsb = lsb % srcWidth;
                if (newLsb + width <= srcWidth) {
                    APPLYING(PUSH_SEL_THROUGH_REPLICATE) {
                        vtxp->fromp(repp->srcp());
                        vtxp->lsb(newLsb);
                    }
                }
            }
        }

        // Sel from Not
        if (DfgNot* const notp = fromp->cast<DfgNot>()) {
            // Replace "Sel from Not" with "Not of Sel"
            if (!notp->hasMultipleSinks()) {
                UASSERT_OBJ(notp->srcp()->dtypep() == notp->dtypep(), notp, "Mismatched widths");
                APPLYING(PUSH_SEL_THROUGH_NOT) {
                    // Make Sel select from source of Not
                    vtxp->fromp(notp->srcp());
                    // Add Not after Sel
                    DfgNot* const replacementp
                        = new DfgNot{m_dfg, notp->fileline(), vtxp->dtypep()};
                    vtxp->replaceWith(replacementp);
                    replacementp->srcp(vtxp);
                }
            }
        }

        // Sel from Sel
        if (DfgSel* const selp = fromp->cast<DfgSel>()) {
            APPLYING(REPLACE_SEL_FROM_SEL) {
                // Make this Sel select from the source of the source Sel
                vtxp->fromp(selp->fromp());
                // Adjust LSB
                vtxp->lsb(lsb + selp->lsb());
            }
        }

        // Sel from Cond
        if (DfgCond* const condp = fromp->cast<DfgCond>()) {
            // If at least one of the branches are a constant, push the select past the cond
            if (condp->thenp()->is<DfgConst>() || condp->elsep()->is<DfgConst>()) {
                APPLYING(PUSH_SEL_THROUGH_COND) {
                    // The new 'then' vertex
                    DfgSel* const newThenp = new DfgSel{m_dfg, flp, vtxp->dtypep()};
                    newThenp->fromp(condp->thenp());
                    newThenp->lsb(lsb);

                    // The new 'else' vertex
                    DfgSel* const newElsep = new DfgSel{m_dfg, flp, vtxp->dtypep()};
                    newElsep->fromp(condp->elsep());
                    newElsep->lsb(lsb);

                    // The replacement Cond vertex
                    DfgCond* const newCondp
                        = new DfgCond{m_dfg, condp->fileline(), vtxp->dtypep()};
                    newCondp->condp(condp->condp());
                    newCondp->thenp(newThenp);
                    newCondp->elsep(newElsep);

                    // Replace this vertex
                    vtxp->replaceWith(newCondp);
                    return;
                }
            }
        }

        // Sel from ShiftL
        if (DfgShiftL* const shiftLp = fromp->cast<DfgShiftL>()) {
            // If selecting bottom bits of left shift, push the Sel before the shift
            if (lsb == 0) {
                UASSERT_OBJ(shiftLp->lhsp()->width() >= width, vtxp, "input of shift narrow");
                APPLYING(PUSH_SEL_THROUGH_SHIFTL) {
                    vtxp->fromp(shiftLp->lhsp());
                    DfgShiftL* const newShiftLp
                        = new DfgShiftL{m_dfg, shiftLp->fileline(), vtxp->dtypep()};
                    vtxp->replaceWith(newShiftLp);
                    newShiftLp->lhsp(vtxp);
                    newShiftLp->rhsp(shiftLp->rhsp());
                }
            }
        }
    }

    //=========================================================================
    //  DfgVertexBinary - bitwise
    //=========================================================================

    void visit(DfgAnd* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (associativeBinary(vtxp)) return;

        commutativeBinary(vtxp);

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();
        FileLine* const flp = vtxp->fileline();

        // Bubble pushing
        if (!vtxp->hasMultipleSinks() && !lhsp->hasMultipleSinks() && !rhsp->hasMultipleSinks()) {
            if (DfgNot* const lhsNotp = lhsp->cast<DfgNot>()) {
                if (DfgNot* const rhsNotp = rhsp->cast<DfgNot>()) {
                    APPLYING(REPLACE_AND_OF_NOT_AND_NOT) {
                        DfgOr* const orp = new DfgOr{m_dfg, flp, vtxp->dtypep()};
                        orp->lhsp(lhsNotp->srcp());
                        orp->rhsp(rhsNotp->srcp());
                        DfgNot* const notp = new DfgNot{m_dfg, flp, vtxp->dtypep()};
                        notp->srcp(orp);
                        vtxp->replaceWith(notp);
                        return;
                    }
                }
                if (DfgNeq* const rhsNeqp = rhsp->cast<DfgNeq>()) {
                    APPLYING(REPLACE_AND_OF_NOT_AND_NEQ) {
                        DfgOr* const orp = new DfgOr{m_dfg, flp, vtxp->dtypep()};
                        orp->lhsp(lhsNotp->srcp());
                        DfgEq* const newRhsp = new DfgEq{m_dfg, rhsp->fileline(), rhsp->dtypep()};
                        newRhsp->lhsp(rhsNeqp->lhsp());
                        newRhsp->rhsp(rhsNeqp->rhsp());
                        orp->rhsp(newRhsp);
                        DfgNot* const notp = new DfgNot{m_dfg, flp, vtxp->dtypep()};
                        notp->srcp(orp);
                        vtxp->replaceWith(notp);
                        return;
                    }
                }
            }
        }

        if (DfgConst* const lhsConstp = lhsp->cast<DfgConst>()) {
            if (lhsConstp->isZero()) {
                APPLYING(REPLACE_AND_WITH_ZERO) {
                    vtxp->replaceWith(lhsConstp);
                    return;
                }
            }

            if (lhsConstp->isOnes()) {
                APPLYING(REMOVE_AND_WITH_ONES) {
                    vtxp->replaceWith(rhsp);
                    return;
                }
            }

            if (DfgConcat* const rhsConcatp = rhsp->cast<DfgConcat>()) {
                if (tryPushBitwiseOpThroughConcat(vtxp, lhsConstp, rhsConcatp)) return;
            }
        }

        if (tryPushBitwiseOpThroughReductions(vtxp)) return;

        if (DfgNot* const lhsNotp = lhsp->cast<DfgNot>()) {
            // ~A & A is all zeroes
            if (lhsNotp->srcp() == rhsp) {
                APPLYING(REPLACE_CONTRADICTORY_AND) {
                    DfgConst* const replacementp = makeZero(flp, vtxp->width());
                    vtxp->replaceWith(replacementp);
                    return;
                }
            }
        }
    }

    void visit(DfgOr* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (associativeBinary(vtxp)) return;

        commutativeBinary(vtxp);

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();
        FileLine* const flp = vtxp->fileline();

        // Bubble pushing
        if (!vtxp->hasMultipleSinks() && !lhsp->hasMultipleSinks() && !rhsp->hasMultipleSinks()) {
            if (DfgNot* const lhsNotp = lhsp->cast<DfgNot>()) {
                if (DfgNot* const rhsNotp = rhsp->cast<DfgNot>()) {
                    APPLYING(REPLACE_OR_OF_NOT_AND_NOT) {
                        DfgAnd* const andp = new DfgAnd{m_dfg, flp, vtxp->dtypep()};
                        andp->lhsp(lhsNotp->srcp());
                        andp->rhsp(rhsNotp->srcp());
                        DfgNot* const notp = new DfgNot{m_dfg, flp, vtxp->dtypep()};
                        notp->srcp(andp);
                        vtxp->replaceWith(notp);
                        return;
                    }
                }
                if (DfgNeq* const rhsNeqp = rhsp->cast<DfgNeq>()) {
                    APPLYING(REPLACE_OR_OF_NOT_AND_NEQ) {
                        DfgAnd* const andp = new DfgAnd{m_dfg, flp, vtxp->dtypep()};
                        andp->lhsp(lhsNotp->srcp());
                        DfgEq* const newRhsp = new DfgEq{m_dfg, rhsp->fileline(), rhsp->dtypep()};
                        newRhsp->lhsp(rhsNeqp->lhsp());
                        newRhsp->rhsp(rhsNeqp->rhsp());
                        andp->rhsp(newRhsp);
                        DfgNot* const notp = new DfgNot{m_dfg, flp, vtxp->dtypep()};
                        notp->srcp(andp);
                        vtxp->replaceWith(notp);
                        return;
                    }
                }
            }
        }

        if (DfgConcat* const lhsConcatp = lhsp->cast<DfgConcat>()) {
            if (DfgConcat* const rhsConcatp = rhsp->cast<DfgConcat>()) {
                if (lhsConcatp->lhsp()->dtypep() == rhsConcatp->lhsp()->dtypep()) {
                    if (lhsConcatp->lhsp()->isZero() && rhsConcatp->rhsp()->isZero()) {
                        APPLYING(REPLACE_OR_OF_CONCAT_ZERO_LHS_AND_CONCAT_RHS_ZERO) {
                            DfgConcat* const replacementp
                                = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
                            replacementp->lhsp(rhsConcatp->lhsp());
                            replacementp->rhsp(lhsConcatp->rhsp());
                            vtxp->replaceWith(replacementp);
                            return;
                        }
                    }
                    if (lhsConcatp->rhsp()->isZero() && rhsConcatp->lhsp()->isZero()) {
                        APPLYING(REPLACE_OR_OF_CONCAT_LHS_ZERO_AND_CONCAT_ZERO_RHS) {
                            DfgConcat* const replacementp
                                = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
                            replacementp->lhsp(lhsConcatp->lhsp());
                            replacementp->rhsp(rhsConcatp->rhsp());
                            vtxp->replaceWith(replacementp);
                            return;
                        }
                    }
                }
            }
        }

        if (DfgConst* const lhsConstp = lhsp->cast<DfgConst>()) {
            if (lhsConstp->isZero()) {
                APPLYING(REMOVE_OR_WITH_ZERO) {
                    vtxp->replaceWith(rhsp);
                    return;
                }
            }

            if (lhsConstp->isOnes()) {
                APPLYING(REPLACE_OR_WITH_ONES) {
                    vtxp->replaceWith(lhsp);
                    return;
                }
            }

            if (DfgConcat* const rhsConcatp = rhsp->cast<DfgConcat>()) {
                if (tryPushBitwiseOpThroughConcat(vtxp, lhsConstp, rhsConcatp)) return;
            }
        }

        if (tryPushBitwiseOpThroughReductions(vtxp)) return;

        if (DfgNot* const lhsNotp = lhsp->cast<DfgNot>()) {
            // ~A | A is all ones
            if (lhsNotp->srcp() == rhsp) {
                APPLYING(REPLACE_TAUTOLOGICAL_OR) {
                    DfgConst* const replacementp = makeZero(flp, vtxp->width());
                    replacementp->num().setAllBits1();
                    vtxp->replaceWith(replacementp);
                    return;
                }
            }
        }
    }

    void visit(DfgXor* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (associativeBinary(vtxp)) return;

        commutativeBinary(vtxp);

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();
        FileLine* const flp = vtxp->fileline();

        if (DfgConst* const lConstp = lhsp->cast<DfgConst>()) {
            if (lConstp->isZero()) {
                APPLYING(REMOVE_XOR_WITH_ZERO) {
                    vtxp->replaceWith(rhsp);
                    return;
                }
            }
            if (lConstp->isOnes()) {
                APPLYING(REPLACE_XOR_WITH_ONES) {
                    DfgNot* const replacementp = new DfgNot{m_dfg, flp, vtxp->dtypep()};
                    replacementp->srcp(rhsp);
                    vtxp->replaceWith(replacementp);
                    return;
                }
            }
            if (DfgConcat* const rConcatp = rhsp->cast<DfgConcat>()) {
                tryPushBitwiseOpThroughConcat(vtxp, lConstp, rConcatp);
                return;
            }
        }

        if (tryPushBitwiseOpThroughReductions(vtxp)) return;
    }

    //=========================================================================
    //  DfgVertexBinary - other
    //=========================================================================

    void visit(DfgAdd* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (associativeBinary(vtxp)) return;

        commutativeBinary(vtxp);
    }

    void visit(DfgArraySel* vtxp) override {
        if (DfgConst* const idxp = vtxp->bitp()->cast<DfgConst>()) {
            if (DfgVarArray* const varp = vtxp->fromp()->cast<DfgVarArray>()) {
                const uint32_t idx = idxp->toU32();
                if (DfgVertex* const driverp = varp->driverAt(idx)) {
                    APPLYING(INLINE_ARRAYSEL) {
                        vtxp->replaceWith(driverp);
                        return;
                    }
                }
            }
        }
    }

    void visit(DfgConcat* vtxp) override {
        UASSERT_OBJ(vtxp->width() == vtxp->lhsp()->width() + vtxp->rhsp()->width(), vtxp,
                    "Inconsistent Concat");

        if (associativeBinary(vtxp)) return;

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();

        FileLine* const flp = vtxp->fileline();

        if (lhsp->isZero()) {
            DfgConst* const lConstp = lhsp->as<DfgConst>();
            if (DfgSel* const rSelp = rhsp->cast<DfgSel>()) {
                if (vtxp->dtypep() == rSelp->fromp()->dtypep()
                    && rSelp->lsb() == lConstp->width()) {
                    APPLYING(REPLACE_CONCAT_ZERO_AND_SEL_TOP_WITH_SHIFTR) {
                        DfgShiftR* const replacementp = new DfgShiftR{m_dfg, flp, vtxp->dtypep()};
                        replacementp->lhsp(rSelp->fromp());
                        replacementp->rhsp(makeI32(flp, lConstp->width()));
                        vtxp->replaceWith(replacementp);
                        return;
                    }
                }
            }
        }

        if (rhsp->isZero()) {
            DfgConst* const rConstp = rhsp->as<DfgConst>();
            if (DfgSel* const lSelp = lhsp->cast<DfgSel>()) {
                if (vtxp->dtypep() == lSelp->fromp()->dtypep() && lSelp->lsb() == 0) {
                    APPLYING(REPLACE_CONCAT_SEL_BOTTOM_AND_ZERO_WITH_SHIFTL) {
                        DfgShiftL* const replacementp = new DfgShiftL{m_dfg, flp, vtxp->dtypep()};
                        replacementp->lhsp(lSelp->fromp());
                        replacementp->rhsp(makeI32(flp, rConstp->width()));
                        vtxp->replaceWith(replacementp);
                        return;
                    }
                }
            }
        }

        if (DfgNot* const lNot = lhsp->cast<DfgNot>()) {
            if (DfgNot* const rNot = rhsp->cast<DfgNot>()) {
                if (!lNot->hasMultipleSinks() && !rNot->hasMultipleSinks()) {
                    APPLYING(PUSH_CONCAT_THROUGH_NOTS) {
                        vtxp->lhsp(lNot->srcp());
                        vtxp->rhsp(rNot->srcp());
                        DfgNot* const replacementp = new DfgNot{m_dfg, flp, vtxp->dtypep()};
                        vtxp->replaceWith(replacementp);
                        replacementp->srcp(vtxp);
                        return;
                    }
                }
            }
        }

        {
            const auto joinSels = [this](DfgSel* lSelp, DfgSel* rSelp, FileLine* flp) -> DfgSel* {
                if (lSelp->fromp()->equals(*rSelp->fromp())) {
                    if (lSelp->lsb() == rSelp->lsb() + rSelp->width()) {
                        // Two consecutive Sels, make a single Sel.
                        const uint32_t width = lSelp->width() + rSelp->width();
                        DfgSel* const joinedSelp = new DfgSel{m_dfg, flp, dtypeForWidth(width)};
                        joinedSelp->fromp(rSelp->fromp());
                        joinedSelp->lsb(rSelp->lsb());
                        return joinedSelp;
                    }
                }
                return nullptr;
            };

            DfgSel* const lSelp = lhsp->cast<DfgSel>();
            DfgSel* const rSelp = rhsp->cast<DfgSel>();
            if (lSelp && rSelp) {
                if (DfgSel* const jointSelp = joinSels(lSelp, rSelp, flp)) {
                    APPLYING(REMOVE_CONCAT_OF_ADJOINING_SELS) {
                        vtxp->replaceWith(jointSelp);
                        return;
                    }
                }
            }
            if (lSelp) {
                if (DfgConcat* const rConcatp = rhsp->cast<DfgConcat>()) {
                    if (DfgSel* const rlSelp = rConcatp->lhsp()->cast<DfgSel>()) {
                        if (DfgSel* const jointSelp = joinSels(lSelp, rlSelp, flp)) {
                            APPLYING(REPLACE_NESTED_CONCAT_OF_ADJOINING_SELS_ON_LHS) {
                                DfgConcat* const replacementp
                                    = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
                                replacementp->lhsp(jointSelp);
                                replacementp->rhsp(rConcatp->rhsp());
                                vtxp->replaceWith(replacementp);
                                return;
                            }
                        }
                    }
                }
            }
            if (rSelp) {
                if (DfgConcat* const lConcatp = lhsp->cast<DfgConcat>()) {
                    if (DfgSel* const lrlSelp = lConcatp->rhsp()->cast<DfgSel>()) {
                        if (DfgSel* const jointSelp = joinSels(lrlSelp, rSelp, flp)) {
                            APPLYING(REPLACE_NESTED_CONCAT_OF_ADJOINING_SELS_ON_RHS) {
                                DfgConcat* const replacementp
                                    = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
                                replacementp->lhsp(lConcatp->lhsp());
                                replacementp->rhsp(jointSelp);
                                vtxp->replaceWith(replacementp);
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    void visit(DfgDiv* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgDivS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgEq* vtxp) override {
        if (foldBinary(vtxp)) return;

        commutativeBinary(vtxp);

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();

        if (DfgConst* const lhsConstp = lhsp->cast<DfgConst>()) {
            if (DfgConcat* const rhsConcatp = rhsp->cast<DfgConcat>()) {
                if (tryPushCompareOpThroughConcat(vtxp, lhsConstp, rhsConcatp)) return;
            }
        }
    }

    void visit(DfgGt* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgGtS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgGte* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgGteS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLogAnd* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLogEq* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLogIf* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLogOr* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLt* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLtS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLte* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgLteS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgModDiv* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgModDivS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgMul* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (associativeBinary(vtxp)) return;

        commutativeBinary(vtxp);
    }

    void visit(DfgMulS* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (associativeBinary(vtxp)) return;

        commutativeBinary(vtxp);
    }

    void visit(DfgNeq* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgPow* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgPowSS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgPowSU* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgPowUS* vtxp) override {
        if (foldBinary(vtxp)) return;
    }

    void visit(DfgReplicate* vtxp) override {
        if (vtxp->dtypep() == vtxp->srcp()->dtypep()) {
            APPLYING(REMOVE_REPLICATE_ONCE) {
                vtxp->replaceWith(vtxp->srcp());
                return;
            }
        }

        if (foldBinary(vtxp)) return;
    }

    void visit(DfgShiftL* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched width");

        if (foldBinary(vtxp)) return;

        optimizeShiftRHS(vtxp);
    }

    void visit(DfgShiftR* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched width");

        if (foldBinary(vtxp)) return;

        optimizeShiftRHS(vtxp);
    }

    void visit(DfgShiftRS* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched width");

        if (foldBinary(vtxp)) return;

        optimizeShiftRHS(vtxp);
    }

    void visit(DfgSub* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->lhsp()->dtypep(), vtxp, "Mismatched LHS width");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->rhsp()->dtypep(), vtxp, "Mismatched RHS width");

        if (foldBinary(vtxp)) return;

        DfgVertex* const lhsp = vtxp->lhsp();
        DfgVertex* const rhsp = vtxp->rhsp();

        if (DfgConst* const rConstp = rhsp->cast<DfgConst>()) {
            if (rConstp->isZero()) {
                APPLYING(REMOVE_SUB_ZERO) {
                    vtxp->replaceWith(lhsp);
                    return;
                }
            }
            if (vtxp->dtypep() == m_bitDType && rConstp->toU32() == 1) {
                APPLYING(REPLACE_SUB_WITH_NOT) {
                    DfgNot* const replacementp = new DfgNot{m_dfg, vtxp->fileline(), m_bitDType};
                    replacementp->srcp(lhsp);
                    vtxp->replaceWith(replacementp);
                    return;
                }
            }
        }
    }

    //=========================================================================
    //  DfgVertexTernary
    //=========================================================================

    void visit(DfgCond* vtxp) override {
        UASSERT_OBJ(vtxp->dtypep() == vtxp->thenp()->dtypep(), vtxp, "Width mismatch");
        UASSERT_OBJ(vtxp->dtypep() == vtxp->elsep()->dtypep(), vtxp, "Width mismatch");

        DfgVertex* const condp = vtxp->condp();
        DfgVertex* const thenp = vtxp->thenp();
        DfgVertex* const elsep = vtxp->elsep();
        FileLine* const flp = vtxp->fileline();

        if (condp->dtypep() != m_bitDType) return;

        if (condp->isOnes()) {
            APPLYING(REMOVE_COND_WITH_TRUE_CONDITION) {
                vtxp->replaceWith(thenp);
                return;
            }
        }

        if (condp->isZero()) {
            APPLYING(REMOVE_COND_WITH_FALSE_CONDITION) {
                vtxp->replaceWith(elsep);
                return;
            }
        }

        if (DfgNot* const condNotp = condp->cast<DfgNot>()) {
            if (!condp->hasMultipleSinks() || condNotp->hasMultipleSinks()) {
                APPLYING(SWAP_COND_WITH_NOT_CONDITION) {
                    vtxp->condp(condNotp->srcp());
                    vtxp->thenp(elsep);
                    vtxp->elsep(thenp);
                    return;
                }
            }
        }

        if (DfgNeq* const condNeqp = condp->cast<DfgNeq>()) {
            if (!condp->hasMultipleSinks()) {
                APPLYING(SWAP_COND_WITH_NEQ_CONDITION) {
                    DfgEq* const newCondp = new DfgEq{m_dfg, condp->fileline(), condp->dtypep()};
                    newCondp->lhsp(condNeqp->lhsp());
                    newCondp->rhsp(condNeqp->rhsp());
                    vtxp->condp(newCondp);
                    vtxp->thenp(elsep);
                    vtxp->elsep(thenp);
                    return;
                }
            }
        }

        if (DfgNot* const thenNotp = thenp->cast<DfgNot>()) {
            if (DfgNot* const elseNotp = elsep->cast<DfgNot>()) {
                if ((!thenp->hasMultipleSinks() || thenNotp->hasMultipleSinks())
                    && (!elsep->hasMultipleSinks() || elsep->hasMultipleSinks())) {
                    APPLYING(PULL_NOTS_THROUGH_COND) {
                        DfgNot* const replacementp
                            = new DfgNot{m_dfg, thenp->fileline(), vtxp->dtypep()};
                        vtxp->thenp(thenNotp->srcp());
                        vtxp->elsep(elseNotp->srcp());
                        vtxp->replaceWith(replacementp);
                        replacementp->srcp(vtxp);
                        return;
                    }
                }
            }
        }

        if (vtxp->width() > 1) {
            // 'cond ? a + 1 : a' -> 'a + cond'
            if (DfgAdd* const thenAddp = thenp->cast<DfgAdd>()) {
                if (DfgConst* const constp = thenAddp->lhsp()->cast<DfgConst>()) {
                    if (constp->toI32() == 1) {
                        if (thenAddp->rhsp() == elsep) {
                            APPLYING(REPLACE_COND_INC) {
                                DfgConcat* const extp = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
                                extp->rhsp(condp);
                                extp->lhsp(makeZero(flp, vtxp->width() - 1));
                                FileLine* const thenFlp = thenAddp->fileline();
                                DfgAdd* const addp = new DfgAdd{m_dfg, thenFlp, vtxp->dtypep()};
                                addp->lhsp(thenAddp->rhsp());
                                addp->rhsp(extp);
                                vtxp->replaceWith(addp);
                                return;
                            }
                        }
                    }
                }
            }
            // 'cond ? a - 1 : a' -> 'a - cond'
            if (DfgSub* const thenSubp = thenp->cast<DfgSub>()) {
                if (DfgConst* const constp = thenSubp->rhsp()->cast<DfgConst>()) {
                    if (constp->toI32() == 1) {
                        if (thenSubp->lhsp() == elsep) {
                            APPLYING(REPLACE_COND_DEC) {
                                DfgConcat* const extp = new DfgConcat{m_dfg, flp, vtxp->dtypep()};
                                extp->rhsp(condp);
                                extp->lhsp(makeZero(flp, vtxp->width() - 1));
                                FileLine* const thenFlp = thenSubp->fileline();
                                DfgSub* const subp = new DfgSub{m_dfg, thenFlp, vtxp->dtypep()};
                                subp->lhsp(thenSubp->lhsp());
                                subp->rhsp(extp);
                                vtxp->replaceWith(subp);
                                return;
                            }
                        }
                    }
                }
            }
        }

        if (vtxp->dtypep() == m_bitDType) {
            AstNodeDType* const dtypep = vtxp->dtypep();
            if (thenp->isZero()) {  // a ? 0 : b becomes ~a & b
                APPLYING(REPLACE_COND_WITH_THEN_BRANCH_ZERO) {
                    DfgAnd* const repalcementp = new DfgAnd{m_dfg, flp, dtypep};
                    DfgNot* const notp = new DfgNot{m_dfg, flp, dtypep};
                    notp->srcp(condp);
                    repalcementp->lhsp(notp);
                    repalcementp->rhsp(elsep);
                    vtxp->replaceWith(repalcementp);
                    return;
                }
            }
            if (thenp->isOnes()) {  // a ? 1 : b becomes a | b
                APPLYING(REPLACE_COND_WITH_THEN_BRANCH_ONES) {
                    DfgOr* const repalcementp = new DfgOr{m_dfg, flp, dtypep};
                    repalcementp->lhsp(condp);
                    repalcementp->rhsp(elsep);
                    vtxp->replaceWith(repalcementp);
                    return;
                }
            }
            if (elsep->isZero()) {  // a ? b : 0 becomes a & b
                APPLYING(REPLACE_COND_WITH_ELSE_BRANCH_ZERO) {
                    DfgAnd* const repalcementp = new DfgAnd{m_dfg, flp, dtypep};
                    repalcementp->lhsp(condp);
                    repalcementp->rhsp(thenp);
                    vtxp->replaceWith(repalcementp);
                    return;
                }
            }
            if (elsep->isOnes()) {  // a ? b : 1 becomes ~a | b
                APPLYING(REPLACE_COND_WITH_ELSE_BRANCH_ONES) {
                    DfgOr* const repalcementp = new DfgOr{m_dfg, flp, dtypep};
                    DfgNot* const notp = new DfgNot{m_dfg, flp, dtypep};
                    notp->srcp(condp);
                    repalcementp->lhsp(notp);
                    repalcementp->rhsp(thenp);
                    vtxp->replaceWith(repalcementp);
                    return;
                }
            }
        }
    }

#undef APPLYING

    // Process one vertex. Return true if graph changed
    void processVertex(DfgVertex* vtxp) {
        // If it has no sinks (unused), we can remove it
        if (!vtxp->hasSinks()) {
            vtxp->unlinkDelete(m_dfg);
            m_changed = true;
            return;
        }

        // Transform node
        iterate(vtxp);

        // If it became unused, we can remove it
        if (!vtxp->hasSinks()) {
            UASSERT_OBJ(m_changed, vtxp, "'m_changed' must be set if node became unused");
            vtxp->unlinkDelete(m_dfg);
        }
    }

    V3DfgPeephole(DfgGraph& dfg, V3DfgPeepholeContext& ctx)
        : m_dfg{dfg}
        , m_ctx{ctx} {

        while (true) {
            // Do one pass over the graph in the forward direction.
            m_changed = false;
            for (DfgVertex *vtxp = m_dfg.opVerticesBeginp(), *nextp; vtxp; vtxp = nextp) {
                nextp = vtxp->verticesNext();
                if (VL_LIKELY(nextp)) VL_PREFETCH_RW(nextp);
                processVertex(vtxp);
            }
            if (!m_changed) break;

            // Do another pass in the opposite direction. Alternating directions reduces
            // the pathological complexity with left/right leaning trees.
            m_changed = false;
            for (DfgVertex *vtxp = m_dfg.opVerticesRbeginp(), *nextp; vtxp; vtxp = nextp) {
                nextp = vtxp->verticesPrev();
                if (VL_LIKELY(nextp)) VL_PREFETCH_RW(nextp);
                processVertex(vtxp);
            }
            if (!m_changed) break;
        }
    }

public:
    static void apply(DfgGraph& dfg, V3DfgPeepholeContext& ctx) { V3DfgPeephole{dfg, ctx}; }
};

void V3DfgPasses::peephole(DfgGraph& dfg, V3DfgPeepholeContext& ctx) {
    V3DfgPeephole::apply(dfg, ctx);
}
