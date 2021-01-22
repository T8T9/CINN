#include <iostream>
#include "cinnrt/common/global.h"
#include "cinnrt/dialect/dense_tensor.h"

#include <llvm/ADT/STLExtras.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/DialectImplementation.h>
#include <mlir/IR/Function.h>
#include <mlir/IR/Module.h>
#include <mlir/IR/OpDefinition.h>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeUtilities.h>
#include <mlir/Support/LogicalResult.h>

#include "cinnrt/dialect/tensor_shape.h"

namespace cinnrt::dt {
using namespace mlir;

void DTDialect::initialize() {
  allowUnknownTypes();
  addOperations<
#define GET_OP_LIST
#include "cinnrt/dialect/dense_tensor.cpp.inc"
      >();
}

namespace detail {
  struct TensorTypeStorage : public mlir::TypeStorage {
    TensorTypeStorage(TargetType target, LayoutType layout,
                      PrecisionType precision): _target(target),
                          _layout(layout), _precision(precision) {}

    using KeyTy = std::tuple<TargetType, LayoutType, PrecisionType>;

    bool operator==(const KeyTy &key) const {
        return key == KeyTy(_target, _layout, _precision);
    }

    static llvm::hash_code hashKey(const KeyTy &key) {
        return llvm::hash_value(key);
    }

    static TensorTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                        const KeyTy &key) {
        return new(allocator.allocate<TensorTypeStorage>())
            TensorTypeStorage(std::get<0>(key), std::get<1>(key), std::get<2>(key));
    }

    TargetType    _target;
    LayoutType    _layout;
    PrecisionType _precision;
  };
} // namespace detail

TargetType getTargetType(mlir::StringRef key) {
  if (key.equals_lower("x86")) return TargetType::X86;
  else if (key.equals_lower("cuda")) return TargetType::CUDA;
  else return TargetType::Unsupported;
}

LayoutType getLayoutType(mlir::StringRef key) {
  if (key.equals_lower("nchw")) return LayoutType::NCHW;
  else if (key.equals_lower("nhwc")) return LayoutType::NHWC;
  else return LayoutType::Unsupported;
}

PrecisionType getPrecisionType(mlir::StringRef key) {
  if (key.equals_lower("i32")) return PrecisionType::I32;
  else if (key.equals_lower("f32")) return PrecisionType::F32;
  else return PrecisionType::Unsupported;
}

TensorType TensorType::get(TargetType target, LayoutType layout, PrecisionType precision) {
    return Base::get(::cinnrt::Global::getMLIRContext(), target, layout, precision);
}

TargetType TensorType::getTarget() {
    return getImpl()->_target;
}

LayoutType TensorType::getLayout() {
    return getImpl()->_layout;
}

PrecisionType TensorType::getPrecision() {
    return getImpl()->_precision;
}

raw_ostream &operator<<(raw_ostream &os, TensorType tensorType) {
  os << "TensorType<"
     << tensorType.getTarget() << ", "
     << tensorType.getLayout() << ", "
     << tensorType.getPrecision() << ">";
  return os;
}

raw_ostream &operator<<(raw_ostream &os, TargetType type) {
  switch(type) {
    case(TargetType::X86):
      os << "X86";
      break;
    case(TargetType::CUDA):
      os << "CUDA";
      break;
    default:
      os << "Unsupported";
  }
  return os;
}

raw_ostream &operator<<(raw_ostream &os, LayoutType type) {
  switch(type) {
    case(LayoutType::NCHW):
      os << "NCHW";
      break;
    case(LayoutType::NHWC):
      os << "NHWC";
      break;
    default:
      os << "Unsupported";
  }
  return os;
}

raw_ostream &operator<<(raw_ostream &os, PrecisionType type) {
  switch(type) {
    case(PrecisionType::I32):
      os << "I32";
      break;
    case(PrecisionType::F32):
      os << "F32";
      break;
    default:
      os << "Unsupported";
  }
  return os;
}

static Type getTensorType(mlir::MLIRContext* context) {
  auto t_dialect = Identifier::get("t", context);
  return OpaqueType::get(t_dialect, "tensor", context);
}

static ParseResult parseCreateUninitTensorOp(OpAsmParser& parser, OperationState& result) {
  auto loc = parser.getCurrentLocation();
  ::mlir::Type outputRawTypes[1];
  ::llvm::ArrayRef<::mlir::Type> outputTypes(outputRawTypes);
  Type attr_type   = IntegerType::get(64, result.getContext());

  Attribute value_attr;
  if (parser.parseAttribute(value_attr, attr_type, "shape", result.attributes)) return failure();

  if (parser.parseArrow()) return failure();
  if (parser.parseType(outputRawTypes[0])) return failure();
  if (!outputRawTypes[0].isa<TensorType>())
    return parser.emitError(loc, "invalid kind of type specified");
  result.addTypes(outputTypes);
  return success();
}

template <typename CreateUninitTensorOp>
static void printCreateUninitTensorOp(OpAsmPrinter& p, CreateUninitTensorOp op) {
  p << CreateUninitTensorOp::getOperationName();
  p << " ";
  p << op.getAttr("shape");
  p << " -> ";
  p << op.getOperation()->getResultTypes();
}

static ParseResult parseFillTensorWithConstantOp(OpAsmParser& parser, OperationState& result) {
  auto loc = parser.getCurrentLocation();
  ::mlir::OpAsmParser::OperandType inputRawOperands[1];
  ::llvm::ArrayRef<::mlir::OpAsmParser::OperandType> inputOperands(inputRawOperands);
  ::mlir::Type inputRawTypes[1];
  ::llvm::ArrayRef<::mlir::Type> inputTypes(inputRawTypes);

  if (parser.parseOperand(inputRawOperands[0])) return failure();

  if (parser.parseColon()) return failure();
  if (parser.parseType(inputRawTypes[0])) return failure();
  if (!inputRawTypes[0].isa<TensorType>())
    return parser.emitError(loc, "invalid kind of type specified");

  Attribute value_attr;
  if (parser.resolveOperands(inputOperands, inputTypes, loc, result.operands)) return failure();
  if (parser.parseAttribute(value_attr, "value", result.attributes)) return failure();
  return success();
}

template <typename FillTensorOp>
static void printFillTensorWithConstantOp(OpAsmPrinter& p, FillTensorOp op) {
  p << FillTensorOp::getOperationName();
  p << " ";
  p.printOperand(op.getOperand());
  p << " : ";
  p << op.getOperation()->getOperandTypes();
  p << " ";
  p << op.getAttr("value");
}

static ParseResult parseSetTensorOp(OpAsmParser& parser, OperationState& result) {
  SmallVector<OpAsmParser::OperandType, 1> operands;
  if (parser.parseOperandList(operands, 1)) return failure();

  auto tensor_type = getTensorType(result.getContext());

  Attribute value_attr;
  return failure(parser.resolveOperand(operands[0], tensor_type, result.operands) ||
                 parser.parseAttribute(value_attr, "values", result.attributes));
}

template <typename SetTensorOp>
static void printSetTensorOp(OpAsmPrinter& p, SetTensorOp op) {
  p << SetTensorOp::getOperationName() << " ";
  p.printOperand(op.getOperand());
  p << " " << op.getAttr("values");
}

#define GET_OP_CLASSES
#include "cinnrt/dialect/dense_tensor.cpp.inc"

}  // namespace cinnrt::dt
