// RUN: cinn-exec -i %s | FileCheck %s

// CHECK-LABEL: test_tensor_type
func @test_tensor_type() {
  %a = dt.create_uninit_tensor.f32 [3, 4] -> !cinn.tensor<X86, NCHW, F32>
  dt.fill_tensor_with_constant.f32 (%a : !cinn.tensor<X86, NCHW, F32>) {value=1.0:f32}
  // CHECK: tensor: shape=shape[3,4], values=[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
  dt.print_tensor (%a : !cinn.tensor<X86, NCHW, F32>)

  cinn.return
}

// TODO:(shibo) support "--verify-diagnostics" and "--split-input-file" options for cinn-exec
//// CHECK-LABEL: error_tensor_type
//func @error_tensor_type() {
//  // expected-error@+1 {{unknown target type: X87}}
//  %a = dt.create_uninit_tensor.f32 [3, 4] -> !cinn.tensor<X87, NCHW, F32>
//  cinn.return
//}
