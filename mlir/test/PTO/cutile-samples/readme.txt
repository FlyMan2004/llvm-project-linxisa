## python接口使用指导

``` bash
export PYTHONPATH=$PYTHONPATH:<llvm-root>/build/tools/mlir/python_packages/mlir_core
```

使用python的接口生成PTO IR的mlir函数：

``` bash
python ./VectorAddition/vadd_pto_ir.py
```

## runop.sh使用指导

mlir/test/PTO/cutile-samples/ 下的每个子文件夹对应一组OP的testcase

使用runop.sh自动执行op的python binding的.py文件生成pto ir和c++代码, 并将生成的.mlir和.cpp文件分别写进对应的子文件夹下

```
#执行所有op的testcase
bash mlir/test/PTO/cutile-samples/runop.sh all
#执行单独的op
bash mlir/test/PTO/cutile-samples/runop.sh -t abs
```
