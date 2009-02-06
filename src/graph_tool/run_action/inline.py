#! /usr/bin/env python
#
# Copyright (C) 2007 Tiago de Paula Peixoto <tiago@forked.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import sys, string, hashlib, os.path, re
from .. import core
from .. import libgraph_tool_core
import numpy

try:
    import scipy.weave
except ImportError:
    raise libgraph_tool_core.raise_error\
          ("You need to have scipy installed to use 'run_action'.")

prefix = None
for d in [d + "/graph_tool" for d in sys.path]:
    if os.path.exists(d):
        prefix = d
        break

inc_prefix = prefix + "/include"
cxxflags = libgraph_tool_core.mod_info().cxxflags + " -I%s" % inc_prefix

# this is the code template which defines the action function object
support_template = open(prefix + "/run_action/run_action_support.hh").read()
code_template = open(prefix + "/run_action/run_action_template.hh").read()

# property map types
props = """
typedef GraphInterface::vertex_index_map_t vertex_index_t;
typedef GraphInterface::edge_index_map_t edge_index_t;
typedef prop_bind_t<GraphInterface::vertex_index_map_t> vertex_prop_t;
typedef prop_bind_t<GraphInterface::edge_index_map_t> edge_prop_t;
typedef prop_bind_t<ConstantPropertyMap<size_t,graph_property_tag> > graph_prop_t;
"""

def clean_prop_type(t):
    return t.replace(" ","_").replace("::","_")\
           .replace("<","_").replace(">","_").\
           replace("__","_")

for d in ["vertex", "edge", "graph"]:
    for t in core.value_types():
        props += "typedef %s_prop_t::as<%s >::type %sprop_%s_t;\n" % \
                 (d,t,d[0],clean_prop_type(t))

def get_graph_type(g):
    return libgraph_tool_core.get_graph_type(g._Graph__graph)

def inline(code, arg_names=[], local_dict=None,
           global_dict=None, force=0, compiler="gcc", verbose=0,
           auto_downcast=1, support_code="", libraries=[],
           library_dirs=[], extra_compile_args=[],
           runtime_library_dirs=[], extra_objects=[],
           extra_link_args=[], mask_ret=[], debug=False):
    """Compile (if necessary) and run the C++ action specified by 'code',
    using weave."""

    # each term on the expansion will properly unwrap a tuple pointer value
    # to a reference with the appropriate name and type
    exp_term = """typename boost::remove_pointer<typename tr1::tuple_element<%d,Args>::type>::type& %s =
                          *tr1::get<%d>(_args);"""
    arg_expansion = "\n".join([ exp_term % (i,arg_names[i],i) for i in \
                                xrange(0, len(arg_names))])

    # we need to get the locals and globals of the _calling_ function. Thus, we
    # need to go deeper into the call stack
    call_frame = sys._getframe(1)
    if local_dict is None:
        local_dict = call_frame.f_locals
    if global_dict is None:
        global_dict = call_frame.f_globals

    # convert variables to boost::python::object, except some known convertible
    # types
    arg_def = props
    arg_conv = ""
    arg_alias = []
    alias_dict = {}
    for arg in arg_names:
        if arg not in local_dict.keys() and arg not in global_dict.keys():
            raise ValueError("undefined variable: "+ arg)
        if arg in local_dict.keys():
            arg_val = local_dict[arg]
        else:
            arg_val = global_dict[arg]
        if issubclass(type(arg_val), core.Graph):
            alias = "__gt__" + arg
            gi = "__gt__" + arg + "__gi"
            graph_type = get_graph_type(arg_val)
            gi_val = arg_val._Graph__graph
            arg_def += "typedef GraphWrap<%s > %s_graph_t;\n" % (graph_type, arg);
            arg_def += "GraphInterface& %s = python::extract<GraphInterface&>(%s);\n" %\
                        (gi, alias)
            arg_def += "%s_graph_t %s = graph_wrap(*boost::any_cast<%s*>(%s.GetGraphView()), %s);\n" % \
                        (arg, arg, graph_type, gi, gi)
            arg_alias.append(alias)
            alias_dict[alias] = gi_val
        elif type(arg_val) == core.PropertyMap:
            alias = "__gt__" + arg
            if arg_val == arg_val.get_graph().vertex_index:
                prop_name = "GraphInterface::vertex_index_map_t"
            elif arg_val == arg_val.get_graph().edge_index:
                prop_name = "GraphInterface::edge_index_map_t"
            else:
                prop_name = "%sprop_%s_t" % \
                            (arg_val.key_type(),
                             clean_prop_type(arg_val.value_type()))
            arg_def  += "%s %s;\n" % (prop_name, arg)
            arg_conv += "%s = get_prop<%s>(%s);\n" % \
                        (arg, prop_name, alias)
            arg_alias.append(alias)
            alias_dict[alias] = arg_val
        elif type(arg_val) not in [int, bool, float, string, numpy.ndarray]:
            alias = "__gt__" + arg
            obj_type = "python::object"
            if type(arg_val) == list:
                obj_type = "python::list"
            elif type(arg_val) == dict:
                obj_type = "python::dict"
            elif type(arg_val) == tuple:
                obj_type = "python::tuple"
            arg_def += "%s %s;\n" % (obj_type, arg)
            arg_conv += "%s = %s(python::object(python::handle<>" % (arg, obj_type) + \
                        "(python::borrowed((PyObject*)(%s)))));\n" % alias
            arg_alias.append(alias)
            alias_dict[alias] = arg_val
        elif type(arg_val) == bool:
            #weave is dumb with bools
            alias = "__gt__" + arg
            arg_def += "bool %s;\n" % arg;
            arg_conv += "%s = python::extract<bool>(python::object(python::handle<>" % arg + \
                        "(python::borrowed((PyObject*)(%s)))));\n" % alias
            arg_alias.append(alias)
            alias_dict[alias] = arg_val
        else:
            arg_alias.append(arg)
            if arg in local_dict.keys():
                alias_dict[arg] = local_dict[arg]
            else:
                alias_dict[arg] = global_dict[arg]

    # handle returned values
    return_vals = ""
    for arg in arg_names:
        if arg in local_dict.keys():
            arg_val = local_dict[arg]
        else:
            arg_val = global_dict[arg]
        if arg not in mask_ret and \
               type(arg_val) not in [numpy.ndarray, core.PropertyMap] and \
               not issubclass(type(arg_val), core.Graph):
            return_vals += 'return_vals["%s"] = %s;\n' % (arg, arg)

    support_code += globals()["support_template"]

    # set debug flag and disable optimization in debug mode
    compile_args = [cxxflags] + extra_compile_args
    if debug:
        compile_args = [re.sub("-O[^ ]*", "", x) for x in compile_args] + ["-g"]

    # insert a hash value into the code below, to force recompilation when
    # support_code (and module version) changes
    support_hash = hashlib.md5(support_code + code + \
                               " ".join(libraries + library_dirs +
                                        [cxxflags] + \
                                        extra_compile_args +\
                                        extra_objects + \
                                        extra_link_args) + \
                               core.__version__).hexdigest()
    code += "\n// support code hash: " + support_hash
    inline_code = string.Template(globals()["code_template"]).\
                  substitute(var_defs=arg_def, var_extract=arg_conv,
                             code=code, return_vals=return_vals)

    # RTLD_GLOBAL needs to be set in dlopen() if we want typeinfo and
    # friends to work properly across DSO boundaries. See
    # http://gcc.gnu.org/faq.html#dso
    orig_dlopen_flags = sys.getdlopenflags()
    sys.setdlopenflags(core.RTLD_NOW|core.RTLD_GLOBAL)

    # call weave and pass all the updated kw arguments
    ret_vals = \
             scipy.weave.inline(inline_code, arg_alias, force=force,
                                local_dict=alias_dict, global_dict=global_dict,
                                compiler=compiler, verbose=verbose,
                                auto_downcast=auto_downcast,
                                support_code=support_code,
                                libraries=libraries,
                                library_dirs=sys.path + library_dirs,
                                extra_compile_args=compile_args,
                                runtime_library_dirs=runtime_library_dirs,
                                extra_objects=extra_objects,
                                extra_link_args=["-Wl,-E "] + extra_link_args)
    # check if exception was thrown
    if ret_vals["__exception_thrown"]:
        libgraph_tool_core.raise_error(ret_vals["__exception_error"])
    else:
        del ret_vals["__exception_thrown"]
        del ret_vals["__exception_error"]
    sys.setdlopenflags(orig_dlopen_flags) # reset dlopen to normal case to
                                          # avoid unnecessary symbol collision
    # set return vals
    for arg in arg_names:
        if ret_vals.has_key(arg):
            if local_dict.has_key(arg):
                local_dict[arg] = ret_vals[arg]
            else:
                global_dict[arg] = ret_vals[arg]
    return ret_vals
