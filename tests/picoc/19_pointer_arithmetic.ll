; ModuleID = 'out.bc'
source_filename = "sparse"

@.str = private constant [4 x i8] c"%d\0A\00"
@.str.1 = private constant [11 x i8] c"b is NULL\0A\00"
@.str.2 = private constant [15 x i8] c"b is not NULL\0A\00"
@.str.3 = private constant [11 x i8] c"c is NULL\0A\00"

define i32 @main() {
L0:
  %a_000001FAB8ADE078 = alloca i32
  %0 = bitcast i32* %a_000001FAB8ADE078 to i8*
  %1 = getelementptr inbounds i8, i8* %0, i64 0
  %2 = bitcast i8* %1 to i32*
  store i32 42, i32* %2
  %R5 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str, i64 0, i64 0), i32 42)
  %cond = icmp ne i32* %a_000001FAB8ADE078, null
  br i1 %cond, label %L2, label %L1

L1:                                               ; preds = %L0
  %R9 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([11 x i8], [11 x i8]* @.str.1, i64 0, i64 0))
  br label %L3

L2:                                               ; preds = %L0
  %R11 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @.str.2, i64 0, i64 0))
  br label %L3

L3:                                               ; preds = %L2, %L1
  %R15 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([11 x i8], [11 x i8]* @.str.3, i64 0, i64 0))
  ret i32 0
}

declare i32 @printf(i8*, ...)
