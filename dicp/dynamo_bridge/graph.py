import logging
import operator
import os
import re
import sys
import time
import torch
import torch.fx

from torch._dynamo import config as dynamo_config
from torch._dynamo.utils import dynamo_timed
from torch._subclasses import FakeTensor, FakeTensorMode

log = logging.getLogger(__name__)

class GraphTransformer:
    def __init__(
        self,
        gm: torch.fx.GraphModule,
        backend: str,
    ):
        self.gm = gm
        if backend == 'topsgraph':
            from dicp.vendor.TopsGraph.opset_transform import topsgraph_opset_transform
            self.backend_opset_transform = topsgraph_opset_transform
            from dicp.vendor.TopsGraph.codegen.enflame import EnflameCodegen
            self.backend_codegen = EnflameCodegen
        elif backend == 'ascendgraph':
            from dicp.vendor.AscendGraph.opset_convert import ascendgraph_opset_convert
            self.backend_opset_transform = ascendgraph_opset_convert
            from dicp.vendor.AscendGraph.codegen.ascend import AscendCodegen
            self.backend_codegen = AscendCodegen

    def transform(self):
        self.aten_gm = self.gm
        self.gm = self.backend_opset_transform(self.gm)

    def infer_shape_dtype(self):
        for n in self.gm.graph.nodes:
            if n.op == 'call_function':
                n.meta['val'] = (n.target(*n.args, **n.kwargs))
            elif n.op == 'get_attr':
                target_atoms = n.target.split('.')
                attr_itr = self.gm
                for i, atom in enumerate(target_atoms):
                    if not hasattr(attr_itr, atom):
                        raise RuntimeError(f"Node referenced nonexistent target {'.'.join(target_atoms[:i])}")
                    attr_itr = getattr(attr_itr, atom)
                    attr_size, attr_dtye = attr_itr.shape, attr_itr.dtype
                with FakeTensorMode():
                    n.meta['val'] = torch.empty(attr_size, dtype=attr_dtye)

    def get_output_shape(self):
        code = self.gm.print_readable(False).split('\n')
        ret_state = []
        self.output_shape = {}
        for idx in reversed(range(len(code))):
            line = code[idx]
            if 'return (' in line:
                pos = line.find('return (')
                line = line[pos + 8:]
                pos = line.find(')')
                line = line[:pos]
                ret_state = line.split(', ')
                break

        for idx, ret in enumerate(ret_state):
            ret_state[idx] = ret.replace(',', '')

        for ret in ret_state:
            shape = None
            ret_str = ret + ': '
            for idx in reversed(range(len(code))):
                line = code[idx]
                if ret_str in line and '[' in line and not 'return (' in line:
                    pos = line.find('[')
                    line = line[pos + 1:]
                    pos = line.find(']')
                    line = line[:pos]
                    shape = line.split(', ')
                    self.output_shape.update({ret: shape})
                    break
            assert shape is not None

    def codegen(self):
        from dicp.vendor.AscendGraph.codegen.ascend import AscendCodegen
        if self.backend_codegen in [AscendCodegen]:
            return self.backend_codegen(self.gm, self.aten_gm).codegen(self.output_shape)
        return self.backend_codegen(self.gm, self.aten_gm).codegen()

    @dynamo_timed
    def compile_to_module(self):
        from torch._inductor.codecache import PyCodeCache

        code = self.codegen()

        mod = PyCodeCache.load(code)

        # if dynamo_config.output_code:
        #     log.info("Output code: %s", mod.__file__)
        return mod

    def compile_to_fn(self):
        return self.compile_to_module().call
