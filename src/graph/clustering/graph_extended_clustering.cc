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

// based on code written by Alexandre Hannud Abdo <abdo@member.fsf.org>

#include "graph_filtering.hh"
#include "graph_selectors.hh"
#include "graph_properties.hh"

#include "graph_extended_clustering.hh"

#include <boost/python.hpp>

using namespace std;
using namespace boost;
using namespace boost::lambda;
using namespace graph_tool;

template <class PropertySequence>
struct prop_vector
{
    boost::any operator()(const vector<boost::any>& props) const
    {
        boost::any prop_vec;
        mpl::for_each<PropertySequence>
            (lambda::bind<void>(get_prop_vector(), lambda::_1,
                                lambda::var(props), lambda::var(prop_vec)));
        return prop_vec;
    }

    struct get_prop_vector
    {
        template <class Property>
        void operator()(Property, const vector<boost::any>& props,
                        boost::any& prop_vec) const
        {
            if (typeid(Property) == props[0].type())
            {
                try
                {
                    vector<Property> vec;
                    vec.resize(props.size());
                    for (size_t i = 0; i < props.size(); ++i)
                        vec[i] = any_cast<Property>(props[i]);
                    prop_vec = vec;
                }
                catch (bad_any_cast){}
            }
        }
    };
};


struct get_property_vector_type
{
    template <class Property>
    struct apply
    {
        typedef vector<Property> type;
    };
};

void extended_clustering(GraphInterface& g, python::list props)
{
    vector<any> cmaps(python::len(props));
    for (size_t i = 0; i < cmaps.size(); ++i)
        cmaps[i] = python::extract<boost::any>(props[i])();

    boost::any vprop = prop_vector<vertex_scalar_properties>()(cmaps);
    if (vprop.empty())
        throw GraphException("all vertex properties must be of the same"
                             " floating point type");

    typedef mpl::transform<writable_vertex_scalar_properties,
                           get_property_vector_type>::type
        properties_vector;

    run_action<>()
        (g, lambda::bind<void>(get_extended_clustering(), lambda::_1,
                               any_cast<GraphInterface::vertex_index_map_t>
                               (g.GetVertexIndex()), lambda::_2),
         properties_vector()) (vprop);
}

