#! /usr/bin/env python
# graph_tool.py -- a general graph manipulation python module
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

"""
``graph_tool`` - a general graph manipulation python module
===========================================================

Provides
   1. A Graph object for graph representation and manipulation
   2. Property maps for Vertex, Edge or Graph.
   3. Fast algorithms implemented in C++.

How to use the documentation
----------------------------

Documentation is available in two forms: docstrings provided
with the code, and the full documentation available in
`the graph-tool homepage <http://graph-tool.forked.de>`_.

We recommend exploring the docstrings using `IPython
<http://ipython.scipy.org>`_, an advanced Python shell with TAB-completion and
introspection capabilities.

The docstring examples assume that ``graph_tool.all`` has been imported as
``gt``::

   >>> import graph_tool.all as gt

Code snippets are indicated by three greater-than signs::

   >>> x = x + 1

Use the built-in ``help`` function to view a function's docstring::

   >>> help(gt.Graph)

Available subpackages
---------------------
clustering
   clustering (aka. transitivity)
community
   community detection
correlations
   vertex and edge correlations
draw
   Graph drawing using graphviz
generation
   random graph generation
misc
   miscenalenous algorithms
stats
   vertex and edge statistics
util
   assorted untilities

Utilities
---------
test
   Run ``graph_tool`` unittests
show_config
   Show ``graph_tool`` build configuration
__version__
   ``graph_tool`` version string

"""

__author__="Tiago de Paula Peixoto <tiago@forked.de>"
__copyright__="Copyright 2008 Tiago de Paula Peixoto"
__license__="GPL version 3 or above"
__URL__="http://graph-tool.forked.de"

from . core import  __version__, Graph, GraphError, Vector_bool, \
     Vector_int32_t, Vector_int64_t, Vector_double, Vector_long_double,\
     Vector_string, value_types, load_graph, PropertyMap, Vertex, Edge

__all__ = ["Graph", "Vertex", "Edge", "GraphError", "Vector_bool",
           "Vector_int32_t", "Vector_int64_t", "Vector_double",
           "Vector_long_double", "Vector_string", "value_types", "load_graph",
           "PropertyMap"]
