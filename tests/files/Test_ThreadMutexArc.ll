; Tests a rust-like thread spawn routine where we pass an Arc<Mutex<i32>> into a "thread".
; The thread is simulated by the implicit addition of ThreadSpawn_Unchecked2 --> vtable_shim.
; ..where @some_im's %load should point to {x} only.
;

%"std::sync::mutex::Mutex<i32>" = type { 
    %"std::sys::pal::unix::locks::futex_mutex::Mutex", ; 4
    %"std::sync::poison::Flag", ; 1
    [3 x i8], ; 3
    ptr ; 8
}
%"std::sys::pal::unix::locks::futex_mutex::Mutex" = type { %"core::sync::atomic::AtomicU32" }
%"core::sync::atomic::AtomicU32" = type { i32 }
%"std::sync::poison::Flag" = type { %"core::sync::atomic::AtomicBool" }
%"core::sync::atomic::AtomicBool" = type { i8 }
%"core::sync::atomic::AtomicUsize" = type { i64 }
%"alloc::sync::ArcInner<std::sync::mutex::Mutex<i32>>" = type {
    %"core::sync::atomic::AtomicUsize", ; 8
    %"core::sync::atomic::AtomicUsize", ; 8
    %"std::sync::mutex::Mutex<i32>", ; 16
    [1 x i32] ; 4
}
%"ClosureThreadBuilderUnchecked" = type { ptr, ptr, ptr, ptr }

define void @vtable_shim(ptr %0) {
    call void @some_im(ptr %0)
    ret void
}

define void @some_im(ptr %0) {
    %gep = getelementptr inbounds %"ClosureThreadBuilderUnchecked", ptr %0, i32 0, i32 3
    %load = load ptr, ptr %gep ; some ptr
    call void @user_routine(ptr %load)
    ret void
}

define void @user_routine(ptr %data) {
    %inner = call ptr @deref(ptr %data)
    %load = load ptr, ptr %inner
    ret void
}

; similar to core::ops::deref::Deref on the mutex, but usually odne on the guard.
define ptr @deref(ptr %self) {
    %self1 = load ptr, ptr %self
    %_0 = getelementptr inbounds %"alloc::sync::ArcInner<std::sync::mutex::Mutex<i32>>", ptr %self1, i64 0, i32 2, i32 3
    ret ptr %_0
}

define void @CreateMutex(ptr sret(%"std::sync::mutex::Mutex<i32>") %_0, i32 %t) {
  %_6 = alloca %"core::sync::atomic::AtomicBool", align 1
  %_5 = alloca %"core::sync::atomic::AtomicU32", align 4
  %_3 = alloca %"std::sync::poison::Flag", align 1
  %_2 = alloca %"std::sys::pal::unix::locks::futex_mutex::Mutex", align 4
  store i32 0, ptr %_5, align 4
  store i8 0, ptr %_6, align 1

  call void @llvm.memcpy.p0.p0.i64(ptr %_2, ptr %_5, i64 4, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr %_3, ptr %_6, i64 1, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr %_0, ptr %_2, i64 4, i1 false)

  %1 = getelementptr inbounds %"std::sync::mutex::Mutex<i32>", ptr %_0, i32 0, i32 1
  %2 = getelementptr inbounds %"std::sync::mutex::Mutex<i32>", ptr %_0, i32 0, i32 3

  call void @llvm.memcpy.p0.p0.i64(ptr %1, ptr %_3, i64 1, i1 false)

  %user_data = alloca ptr
  ; store i32 1234, ptr %2, align 8
  store ptr %user_data, ptr %2

  ret void
}

define ptr @CreateArc(ptr %data) {
start:
  %_4 = alloca %"core::sync::atomic::AtomicUsize", align 8
  %_3 = alloca %"alloc::sync::ArcInner<std::sync::mutex::Mutex<i32>>", align 8

  store i64 1, ptr %_4, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr %_3, ptr %_4, i64 8, i1 false)

  %0 = getelementptr inbounds %"alloc::sync::ArcInner<std::sync::mutex::Mutex<i32>>", ptr %_3, i32 0, i32 1
  call void @llvm.memcpy.p0.p0.i64(ptr %0, ptr readonly align 8 %_4, i64 8, i1 false) #18

  %1 = getelementptr inbounds %"alloc::sync::ArcInner<std::sync::mutex::Mutex<i32>>", ptr %_3, i32 0, i32 2
  call void @llvm.memcpy.p0.p0.i64(ptr %1, ptr %data, i64 16, i1 false)

  %_4.i = call ptr @_ZN5alloc5alloc15exchange_malloc17hb035d36935001f69E()
  br label %"_ZN5alloc5boxed12Box$LT$T$GT$3new17h5bb528b017a0f190E.exit"

"_ZN5alloc5boxed12Box$LT$T$GT$3new17h5bb528b017a0f190E.exit": ; preds = %start
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %_4.i, ptr align 8 %_3, i64 36, i1 false)
  ret ptr %_4.i
}

define void @ThreadSpawn(ptr %data) {
    call void @ThreadSpawn_Unchecked(ptr %data)
    ret void
}

define void @ThreadSpawn_Unchecked(ptr %data) {
    call void @ThreadSpawn_Unchecked2(ptr %data)
    ret void
}

define void @ThreadSpawn_Unchecked2(ptr %data) {
    %f = alloca ptr
    store ptr %data, ptr %f

    %main = alloca %"ClosureThreadBuilderUnchecked", align 8
    %other = alloca %"ClosureThreadBuilderUnchecked", align 8

    %s1 = getelementptr inbounds %"ClosureThreadBuilderUnchecked", ptr %main, i64 0, i32 3
    store ptr %data, ptr %s1

    call void @llvm.memcpy.p0.p0.i64(ptr %other, ptr %main, i64 32, i1 false)

    %thread_data = call ptr @_ZN5alloc5alloc15exchange_malloc17hb035d36935001f69E()
    call void @llvm.memcpy.p0.p0.i64(ptr %thread_data, ptr %other, i64 32, i1 false)
    ret void
}

define void @main() {
    %_2 = alloca %"std::sync::mutex::Mutex<i32>", align 4
    call void @CreateMutex(ptr sret(%"std::sync::mutex::Mutex<i32>") %_2, i32 undef)

    %arc = call ptr @CreateArc(ptr %_2)

    %x = alloca ptr
    store ptr %arc, ptr %x

    call void @ThreadSpawn(ptr %x)
    ret void
}


declare ptr @_ZN5alloc5alloc15exchange_malloc17hb035d36935001f69E() #0
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)

attributes #0 = { inlinehint nonlazybind allockind("alloc") }
