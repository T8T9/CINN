#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cinn/common/graph_utils.h"
#include "cinn/ir/buffer.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/optim/buffer_assign.h"
#include "cinn/optim/compute_inline_expand.h"
#include "cinn/optim/fold_cinn_call_arguments.h"
#include "cinn/optim/optimize.h"
#include "cinn/optim/remove_nested_block.h"
#include "cinn/optim/replace_call_with_expr.h"
#include "cinn/optim/tensor_write_tell.h"
#include "cinn/optim/transform_gpu_forloop.h"
#include "cinn/optim/transform_polyfor_to_for.h"
#include "cinn/poly/ast_gen.h"

namespace cinn {

namespace poly {
class Stage;
}  // namespace poly

namespace lang {
namespace detail {

/**
 * After the AstGen build the forloop from isl exprs, all the ISL Call nodes should be mapped to the corresponding CINN
 * expressions, there should be no remaining.
 */
void CheckNoIslCallRemains(const Expr* expr);

/**
 * \brief Lower a single group of nodes.
 *
 * We partition the whole computation of a function into several groups, each group is a basic element for ISL
 * polyhedral computation, that is, we transform a group into a isl domain and schedule, and generate ast latter.
 *
 * @param group A single schedule group containing several Stages and the scheduling order.
 * @param tuple_to_expr A map from isl set tuple name to CINN expressions.
 */
Expr LowerGroup(const poly::ScheduleGroup& group,
                const std::map<std::string, Expr>& tuple_to_expr,
                std::map<std::string, Tensor>* global_tensor_map);

/**
 * A Computation graph node.
 */
struct CompuGraphNode : public common::GraphNode {
  explicit CompuGraphNode(ir::Tensor tensor) : tensor(tensor) {}

  ir::Tensor tensor;

  std::string id() const override;
  const char* type_info() const override;
  static const char* __type_info__;
};

/**
 * \brief Create a computation graph using a tensor set.
 * It will deduce the temporary tensors not in the \p tensors.
 * It consider the `extra_depend_stages` stored in tensor.stage.
 *
 * @param tensors the input/output tensors of a computation.
 * @param hide_inline hide inline tensor nodes.
 * @return a graph.
 */
std::unique_ptr<common::Graph> CreateCompGraph(const std::vector<ir::Tensor>& tensors, bool hide_inline = false);

class LowerImpl {
 public:
  /**
   * @param fn_name the name of the final output function.
   * @param tensor_args the tensor arguments for the function
   * @param scalar_args the scalar arguments for the function
   * @param temp_tensor_args the extra temporary tensor arguments
   *
   * The \p tensor_args contains both input and output tensors.
   */
  LowerImpl(const std::string& fn_name,
            const std::vector<Tensor>& tensor_args,
            const std::vector<Var>& scalar_args,
            const std::vector<Tensor>& temp_tensor_args = {})
      : fn_name_(fn_name), tensor_args_(tensor_args), scalar_args_(scalar_args), temp_tensor_args_(temp_tensor_args) {
    std::vector<ir::Tensor> tensors(tensor_args.begin(), tensor_args.end());
    tensors.insert(std::end(tensors), temp_tensor_args.begin(), temp_tensor_args.end());
    compu_graph_ = CreateCompGraph(tensors, true /*hide_inlined*/);

    LOG(INFO) << "Computation Graph:\n" << compu_graph_->Visualize();
  }

  ir::LoweredFunc operator()();

  /**
   * Get the computational graph.
   */
  const common::Graph* comp_graph() const { return compu_graph_.get(); }

  /**
   * \brief generate the argument list of the final output function.
   * We put the scalar_args in front of tensor_args, e.g. get tensor_args{A,B}, scalar_args{m}, the final argument list
   * is {m, A, B}, the input and output tensor can be mixed in the tensor_args, the kInput and kOutput token will deduce
   * from their usage in the computation.
   */
  std::vector<ir::Argument> GenerateFunctionArgumentList(Expr fn_body);

  /**
   * \brief generate the body expression of the final output function.
   */
  Expr GenerateFunctionBody(const poly::Schedule* schedule);

 private:
  /**
   * \brief Collect the temporary tensors.
   * A temporary tensor is one that is in the computation graph, not inlined and not in the tensor_args(similar to a
   * temporary variable inside function).
   */
  std::vector<Tensor> CollectTemporaryTensors();

  /**
   * \brief Check both the tensor_args and sclar_args not contain duplication (different arguemnt with the same name).
   */
  void CheckArgsUnique();

  /**
   * \brief Get a map, for each tensor in the tensor_args, map from name to itself.
   */
  inline std::unordered_map<std::string, Tensor> GenTensorArgMap();

  /**
   * \brief Get a map, for each tensor in the computation graph, map from name to itself.
   */
  inline std::unordered_map<std::string, Tensor> GenAllTensorMap();

  /**
   * \brief Get all the tensors, including the input, output and temporary ones.
   */
  std::vector<Tensor> CollectAllTensors();

  /**
   * \brief Collect the extra dependencies between tensors.
   *
   * The extra dependencies include
   * 1. the control deps in Stage.
   *
   * TODO(Superjomn) remove the field `extra_depend_stages`
   */
  std::set<std::pair<std::string, std::string>> CollectExtraDependencies() const;

 private:
  const std::string& fn_name_;
  const std::vector<Tensor>& tensor_args_;
  const std::vector<Var>& scalar_args_;
  const std::vector<Tensor>& temp_tensor_args_;

  //! A computation graph generated from the tensor_args and scalar_args.
  std::unique_ptr<common::Graph> compu_graph_;
};

/**
 * \brief Tell whether a tensor contains some GPU related information, such some schedule.
 */
bool TensorContainsGPUInfo(ir::Tensor t);

/**
 * Deal with the `compute_at` transform, in the stage transform phase, we modified the domain and transform of the
 * producer tensor, after isl Ast generation, there remains some postprocess here include
 *
 * 1. in producer tensor load, make each axis to zero
 * 2. add offset
 *
 * e.g.
 *
 * auto A_cache = Compute({M, N}, [&](Expr i, Expr j) { return A(i, j); }, "cache");
 * auto C = Compute(
 *     {Expr(10), Expr(10)}, [&](Expr i, Expr j) { return A_cache(i, j) + A_cache(i+1,j) + B(i, j); }, "C");
 * A_cache->stage()->ComputeAt2(C->stage(), 0);
 *
 * \code
 * function fn (_A, _B, _cache, _C)
 * {
 *   for (_p0, 10)
 *   {
 *     for (i, 10)
 *     {
 *       if ((i <= 1)) {
 *         for (j, 10)
 *         {
 *           cache[i, j] = A[i, j]
 *         }
 *       }
 *       C[_p0, i] = (cache[_p0, i] + (cache[(1 + _p0), i] + B[_p0, i]))
 *      }
 *   }
 * }
 * \endcode
 *
 * The expression `C[_p0, i] = (cache[_p0, i] + (cache[(1 + _p0), i] + B[_p0, i]))` produces tensor `C`, but the cache
 * should start from zero.
 */
void ProcessComputeAtInfo(Expr* expr);

/**
 * Resize the compute_at consumer buffer size.
 */
void UpdateComputeAtBufferShape(Expr* expr);

/**
 * Mark the PolyFor as Vectorized if it is scheduled Vectorize in Stage.
 */
struct MarkVectorizeMutator : public ir::IRMutator<Expr*> {
  const std::map<std::string, ir::VectorizeInfo>& vectorizes;

  explicit MarkVectorizeMutator(const std::map<std::string /*tensor name*/, ir::VectorizeInfo>& vectorizes)
      : vectorizes(vectorizes) {}

  void operator()(Expr* expr) { ir::IRMutator<Expr*>::Visit(expr, expr); }

  // NOTE This mutator takes PolyFor as input, not For.
  void Visit(const ir::PolyFor* op, Expr* expr) override {
    auto* node = expr->As<ir::PolyFor>();
    stack.push_back(node);
    ir::IRMutator<ir::Expr*>::Visit(op, expr);
    stack.pop_back();
  }

  // each statement in ISL is bound to a Store node.
  void Visit(const ir::Store* op, Expr* expr) override {
    auto* tensor_n = op->tensor.As<ir::_Tensor_>();
    CHECK(tensor_n);
    auto it = vectorizes.find(tensor_n->name);
    if (it != vectorizes.end()) {
      stack[it->second.level]->set_vectorize_info(it->second);
      CHECK(it->second.valid());
    }
  }

  std::vector<ir::PolyFor*> stack;
};

/**
 * Mark the PolyFor as Unroll if is called Unroll in Stage.
 */
struct MarkUnrollMutator : public ir::IRMutator<Expr*> {
  std::map<std::string, std::set<int> /*level*/> unrolls;

  explicit MarkUnrollMutator(const std::map<std::string, std::set<int>>& unrolls) : unrolls(unrolls) {}

  void operator()(Expr* expr) { ir::IRMutator<>::Visit(expr, expr); }

  void Visit(const ir::PolyFor* op, Expr* expr) override {
    auto* node = expr->As<ir::PolyFor>();
    stack.push_back(node);
    ir::IRMutator<>::Visit(op, expr);
    stack.pop_back();
  }

  // each statement in ISL is bound to a Store node.
  void Visit(const ir::Store* op, Expr* expr) override {
    auto* tensor_n = op->tensor.As<ir::_Tensor_>();
    CHECK(tensor_n);
    auto it = unrolls.find(tensor_n->name);
    if (it != unrolls.end()) {
      for (int level : it->second) {
        VLOG(1) << "Mark " << level << " Unrolled";
        CHECK_LT(level, stack.size());
        stack[level]->set_unrolled();
      }
    }
  }

  std::vector<ir::PolyFor*> stack;
};

}  // namespace detail
}  // namespace lang
}  // namespace cinn