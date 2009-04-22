// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2007  Tiago de Paula Peixoto <tiago@forked.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <boost/python.hpp>
#include "graph.hh"

using namespace boost;
using namespace boost::python;
using namespace graph_tool;

void random_rewire(GraphInterface& gi, string strat, bool self_loops,
                   bool parallel_edges, size_t seed);
bool check_isomorphism(GraphInterface& gi1, GraphInterface& gi2,
                       boost::any iso_map);

BOOST_PYTHON_MODULE(libgraph_tool_misc)
{
    def("random_rewire", &random_rewire);
    def("check_isomorphism", &check_isomorphism);
}
