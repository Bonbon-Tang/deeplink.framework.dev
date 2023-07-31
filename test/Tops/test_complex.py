import torch
import torch._dynamo

class MyModule(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.param = torch.nn.Parameter(torch.rand(3, 4))
        self.linear = torch.nn.Linear(4, 5)

    def forward(self, x):
        output = torch.ops.aten.view_as_complex(x)
        output = torch.ops.aten.view_as_real.default(output)
        return output

x = torch.randn(5, 2)

enflame_model = MyModule()
compiled_model = torch.compile(enflame_model, backend="topsgraph")
r1= compiled_model(x)
 
torch._dynamo.reset()

torch_model = MyModule()
r2 = torch_model(x)

print(f"Test complex op result:{torch.allclose(r1, r2, equal_nan=True)}")
