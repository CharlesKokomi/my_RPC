import onnxruntime as ort

# 加载模型
model_path = "models/super_resolution.onnx"
session = ort.InferenceSession(model_path)

# 获取输入节点信息
model_inputs = session.get_inputs()
for input in model_inputs:
    print(f"输入名称: {input.name}")
    print(f"输入形状 (Shape): {input.shape}")
    print(f"输入类型: {input.type}")