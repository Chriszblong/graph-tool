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

from .. dl_import import dl_import
dl_import("import libgraph_tool_generation")

from .. core import Graph
import sys, numpy

__all__ = ["random_graph"]

def random_graph(N, deg_sampler, deg_corr=None, directed=True,
                 parallel=False, self_loops=False,
                 seed=0, verbose=False):
    if seed == 0:
        seed = numpy.random.randint(0, sys.maxint)
    g = Graph()
    if deg_corr == None:
        uncorrelated = True
    else:
        uncorrelated = False
    libgraph_tool_generation.gen_random_graph(g._Graph__graph, N,
                                              deg_sampler, deg_corr,
                                              uncorrelated, not parallel,
                                              not self_loops, not directed,
                                              seed, verbose)
    g.set_directed(directed)
    return g
