#pragma once

#include <memory>
#include <string>

#include "cinn/ir/type.h"

namespace cinn {
namespace ir {

class IRVisitor;

// clang-format off
#define NODETY_PRIMITIVE_TYPE_FOR_EACH(macro__) \
  macro__(IntImm)                               \
  macro__(UIntImm)                              \
  macro__(FloatImm)                             \

#define NODETY_OP_FOR_EACH(macro__) \
  macro__(Add)                      \
  macro__(Sub)                      \
  macro__(Mul)                      \
  macro__(Div)                      \
  macro__(Mod)                      \
  macro__(EQ)                       \
  macro__(NE)                       \
  macro__(LT)                       \
  macro__(LE)                       \
  macro__(GT)                       \
  macro__(GE)                       \
  macro__(And)                      \
  macro__(Or)                       \
  macro__(Not)                      \
  macro__(Min)                      \
  macro__(Max)                      \

#define NODETY_CONTROL_OP_FOR_EACH(macro__) \
  macro__(For)                              \
  macro__(IfThenElse)                       \
  macro__(Block)                            \
  macro__(Call)                             \
  macro__(Cast)                             \
  macro__(Module)

#define NODETY_FORALL(macro__)          \
  NODETY_PRIMITIVE_TYPE_FOR_EACH(__m)   \
  NODETY_OP_FOR_EACH(__m)               \
  NODETY_CONTROL_OP_FOR_EACH(__m)
// clang-format on

//! Define IrNodeTy
// @{
#define __m(x__) x__,
enum class IrNodeTy { NODETY_FORALL(__m) };
#undef __m
// @}

std::ostream& operator<<(std::ostream& os, IrNodeTy type);

class IRNode : public std::enable_shared_from_this<IRNode> {
 public:
  IRNode() = default;
  virtual ~IRNode() = default;

  virtual void Accept(IRVisitor* v) const {}
  virtual IrNodeTy node_type() = 0;
  virtual const Type& type() = 0;

  std::shared_ptr<const IRNode> getptr() const { return shared_from_this(); }
};

/**
 * A handle to store any IRNode.
 */
class IRHandle : public std::enable_shared_from_this<IRHandle> {
 public:
  IRHandle() = default;
  IRHandle(IRHandle& other) : ptr_(other.ptr_) {}
  explicit IRHandle(IRNode* x) { ptr_.reset(x); }
  explicit IRHandle(const std::shared_ptr<IRNode>& x) { ptr_ = x; }

  IrNodeTy node_type() const { return ptr_->node_type(); }
  Type type() const { return ptr_->type(); }

  template <typename T>
  const T* As() const {
    if (node_type() == T::_type_info_) return static_cast<const T*>(ptr_.get());
    return nullptr;
  }
  template <typename T>
  T* As() {
    if (node_type() == T::_node_type_) return static_cast<T*>(ptr_.get());
    return nullptr;
  }

  bool defined() const { return ptr_.get(); }

  const std::shared_ptr<IRNode>& ptr() const { return ptr_; }
  void set_ptr(const std::shared_ptr<IRNode>& x) { ptr_ = x; }

 protected:
  std::shared_ptr<IRNode> ptr_{};
};

template <typename T>
struct ExprNode : public IRNode {
  explicit ExprNode(Type t) : type_(t) {}

  void Accept(IRVisitor* v) const override;

  T* self() { return static_cast<T*>(this); }
  const T* const_self() const { return static_cast<const T*>(this); }

  IrNodeTy node_type() { return T::_node_type_; }
  const Type& type() { return type_; }

 private:
  Type type_;
};

struct IntImm : public ExprNode<IntImm> {
  int64_t value;

  IntImm(Type t, int64_t v) : ExprNode<IntImm>(t), value(v) {
    CHECK(t.is_int());
    CHECK(t.is_scalar());
    CHECK(t.bits() == 8 || t.bits() == 16 || t.bits() == 32 || t.bits() == 64);
  }

  static const IrNodeTy _node_type_ = IrNodeTy::IntImm;
};

struct UIntImm : public ExprNode<UIntImm> {
  int64_t value;

  UIntImm(Type t, int64_t v) : ExprNode<UIntImm>(t), value(v) {
    CHECK(t.is_int());
    CHECK(t.is_scalar());
    CHECK(t.bits() == 8 || t.bits() == 16 || t.bits() == 32 || t.bits() == 64);
  }

  static const IrNodeTy _node_type_ = IrNodeTy::UIntImm;
};

struct FloatImm : public ExprNode<FloatImm> {
  int value;

  FloatImm(Type t, float v) : ExprNode<FloatImm>(t), value(v) {
    CHECK(t.is_float());
    CHECK(t.is_scalar());
  }

  static const IrNodeTy _node_type_ = IrNodeTy::FloatImm;
};

struct Expr : public IRHandle {
 public:
  Expr() = default;
  Expr(const Expr& other) : IRHandle(other.ptr()) {}
  Expr(const std::shared_ptr<IRNode>& p) : IRHandle(p) {}
  void operator=(const std::shared_ptr<IRNode>& p) { ptr_ = p; }

  //! Helper function to construct numeric constants of various types.
  // @{
  explicit Expr(int32_t x) : IRHandle(std::make_shared<IntImm>(Int(32), x)) {}
  explicit Expr(int64_t x) : IRHandle(std::make_shared<IntImm>(Int(64), x)) {}
  explicit Expr(float x) : IRHandle(std::make_shared<IntImm>(Float(32), x)) {}
  explicit Expr(double x) : IRHandle(std::make_shared<IntImm>(Float(64), x)) {}
  // @}
};

template <typename T>
struct UnaryOpNode : public ExprNode<T> {
  UnaryOpNode(Type type, Expr v) : ExprNode<T>(type), v(v) { CHECK(v.defined()); }

  // The single argument.
  Expr v;
};

template <typename T>
struct BinaryOpNode : public ExprNode<T> {
  BinaryOpNode(Type type, Expr a, Expr b) : ExprNode<T>(type), a(a), b(b) {
    CHECK(type.valid());
    CHECK(a.defined());
    CHECK(b.defined());
    CHECK(a.type() == b.type()) << "the two arguments' type not match";
  }

  //! The two arguments.
  Expr a, b;
};

}  // namespace ir
}  // namespace cinn