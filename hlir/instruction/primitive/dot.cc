#include "hlir/instruction/primitive/dot.h"
#include "cinn/common/ir.h"

namespace hlir {
namespace instruction {
namespace primitive {

using cinn::Compute;
using cinn::Expr;
using cinn::Var;

Tensor DotImpl::VecDotVec(const Tensor &a, const Tensor &b, const std::string &name) {
  CHECK(a->SameShapeWith(b)) << "Tensor " << a->name << " and " << b->name << " shape not match for DOT primitive";
  CHECK_EQ(a->shape.size(), 1UL) << "Vector DOT should input one-dimentional tensors";

  Var k(a->shape[0], ctx_->new_var_name("_rv"));
  return cinn::Compute({Expr(1)}, [=]() -> Expr { return cinn::Sum(a(k) * b(k)); }, name, {k});
}

Tensor DotImpl::MatDotMat(const Tensor &a, const Tensor &b, const std::string &name) {
  CHECK_EQ(a->shape.size(), 2UL) << "Matrix DOT, the first input tensor should have 2 dimensions";
  CHECK_EQ(b->shape.size(), 2UL) << "Matrix DOT, the second input tensor should have 2 dimensions";
  CHECK(cinn::common::MathEqual(a->shape[1], b->shape[0]))
      << "1th-input's shape[1] should equal to 2th-input's shape[0], but get " << a->shape[1] << " vs " << b->shape[0];
  Var k(a->shape[1], ctx_->new_var_name("_rv"));

  auto fn = [=](Var i, Var j) -> Expr { return cinn::Sum(a(i, k) * b(k, j)); };

  std::vector<Expr> shape({a->shape[0], b->shape[1]});

  return Compute(shape, fn, name, {k});
}

Tensor DotImpl::MatDotVec(const Tensor &a, const Tensor &b, const std::string &name) {
  CHECK_EQ(a->shape.size(), 2UL);
  CHECK_EQ(b->shape.size(), 1UL);
  CHECK(cinn::common::MathEqual(a->shape[1], b->shape[0]))
      << "shape not match, 1th-input's shape[1] should equal to 2th-input's shape[0], but get " << a->shape[1] << " vs "
      << b->shape[0];

  Var k(a->shape[1], ctx_->new_var_name("_rv"));
  auto fn = [=](Var i) -> Expr { return cinn::Sum(a(i, k) * b(k)); };

  std::vector<Expr> shape({a->shape[0]});
  return Compute(shape, fn, name, {k});
}

Tensor DotImpl::operator()(const Tensor &a, const Tensor &b, const std::string &name) {
  size_t a_dims = a->shape.size();
  size_t b_dims = b->shape.size();

  Tensor res;
  if (a_dims == 2 && b_dims == 2) {
    res = MatDotMat(a, b, name);
  } else if (a_dims == 2 && b_dims == 1) {
    res = MatDotVec(a, b, name);
  } else if (a_dims == 1 && b_dims == 1) {
    res = VecDotVec(a, b, name);
  } else {
    NOT_IMPLEMENTED
  }

  res->WithBuffer();

  return res;
}

}  // namespace primitive
}  // namespace instruction
}  // namespace hlir