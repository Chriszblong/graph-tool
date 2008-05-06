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

#include "graph_filtering.hh"
#include "graph.hh"
#include "graph_python_interface.hh"

#include <boost/python.hpp>
#include <boost/lambda/bind.hpp>

using namespace std;
using namespace boost;
using namespace graph_tool;

// this will register the property maps types and all its possible access
// functions to python
struct export_vertex_property_map
{
    export_vertex_property_map(const string& name, const GraphInterface &gi)
        : _name(name), _gi(gi) {}

    template <class PropertyMap>
    void operator()(PropertyMap) const
    {
        using python::detail::gcc_demangle;

        typedef PythonPropertyMap<PropertyMap> pmap_t;

        string type_name;
        if (is_same<typename mpl::find<value_types,
                                       typename pmap_t::value_type>::type,
                    typename mpl::end<value_types>::type>::value)
            type_name =
                gcc_demangle(typeid(typename pmap_t::value_type).name());
        else
            type_name =
                type_names[mpl::find<value_types,typename pmap_t::value_type>
                           ::type::pos::value];
        string class_name = _name + "<" + type_name + ">";

        typedef typename mpl::if_<
            typename return_reference::apply<typename pmap_t::value_type>::type,
            python::return_internal_reference<>,
            python::return_value_policy<python::return_by_value> >
            ::type return_policy;

        python::class_<pmap_t> pclass(class_name.c_str(),
                                      python::no_init);
        pclass.def("__hash__", &pmap_t::GetHash)
            .def("value_type", &pmap_t::GetType)
            .def("__getitem__", &pmap_t::template GetValue<PythonVertex>,
                 return_policy())
            .def("__setitem__", &pmap_t::template SetValue<PythonVertex>);
    }

    string _name;
    const GraphInterface& _gi;
};

struct export_edge_property_map
{
    export_edge_property_map(const string& name, const GraphInterface &gi)
        : _name(name), _gi(gi) {}

    template <class PropertyMap>
    struct export_access
    {
        typedef PythonPropertyMap<PropertyMap> pmap_t;

        export_access(python::class_<pmap_t>& pclass)
            : _pclass(pclass) {}

        typedef typename mpl::if_<
            typename return_reference::apply<typename pmap_t::value_type>::type,
            python::return_internal_reference<>,
            python::return_value_policy<python::return_by_value> >
            ::type return_policy;

        template <class Graph>
        void operator()(Graph*) const
        {
            _pclass
                .def("__getitem__",
                     &pmap_t::template GetValue<PythonEdge<Graph> >,
                     return_policy())
                .def("__setitem__",
                     &pmap_t::template SetValue<PythonEdge<Graph> >);
        }

        python::class_<PythonPropertyMap<PropertyMap> >& _pclass;
    };

    template <class PropertyMap>
    void operator()(PropertyMap) const
    {

        typedef PythonPropertyMap<PropertyMap> pmap_t;

        string type_name =
            type_names[mpl::find<value_types,
                       typename pmap_t::value_type>::type::pos::value];
        string class_name = _name + "<" + type_name + ">";

        python::class_<pmap_t> pclass(class_name.c_str(),
                                      python::no_init);
        pclass.def("__hash__", &pmap_t::GetHash)
            .def("value_type", &pmap_t::GetType);

        typedef mpl::transform<graph_tool::detail::all_graph_views,
                               mpl::quote1<add_pointer> >::type graph_views;

        mpl::for_each<graph_views>(export_access<PropertyMap>(pclass));
    }

    string _name;
    const GraphInterface& _gi;
};

struct export_graph_property_map
{
    export_graph_property_map(const string& name, const GraphInterface &gi)
        : _name(name), _gi(gi) {}

    template <class PropertyMap>
    void operator()(PropertyMap) const
    {
        typedef PythonPropertyMap<PropertyMap> pmap_t;

        string type_name =
            type_names[mpl::find<value_types,
                       typename pmap_t::value_type>::type::pos::value];
        string class_name = _name + "<" + type_name + ">";

        typedef typename mpl::if_<
            typename return_reference::apply<typename pmap_t::value_type>::type,
            python::return_internal_reference<>,
            python::return_value_policy<python::return_by_value> >
            ::type return_policy;

        python::class_<pmap_t> pclass(class_name.c_str(),
                                      python::no_init);
        pclass.def("__hash__", &pmap_t::GetHash)
            .def("value_type", &pmap_t::GetType)
            .def("__getitem__", &pmap_t::template GetValue<GraphInterface>,
                 return_policy())
            .def("__setitem__", &pmap_t::template SetValue<GraphInterface>);
    }

    string _name;
    const GraphInterface& _gi;
};

void export_python_properties(const GraphInterface& gi)
{
    typedef property_map_types::apply<
        value_types,
        GraphInterface::vertex_index_map_t,
        mpl::bool_<true>
        >::type vertex_property_maps;
    typedef property_map_types::apply<
        value_types,
        GraphInterface::edge_index_map_t,
        mpl::bool_<true>
        >::type edge_property_maps;
    typedef property_map_types::apply<
        value_types,
        ConstantPropertyMap<size_t,graph_property_tag>,
        mpl::bool_<false>
        >::type graph_property_maps;
    mpl::for_each<vertex_property_maps>
        (export_vertex_property_map("VertexPropertyMap", gi));
    mpl::for_each<edge_property_maps>
        (export_edge_property_map("EdgePropertyMap", gi));
    mpl::for_each<graph_property_maps>
        (export_graph_property_map("GraphPropertyMap", gi));
}
