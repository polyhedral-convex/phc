#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <PowerDiagram/support/map_item.h>
#include <polycon/PolyCon.h>

#ifndef POLYCON_SCALAR
#define POLYCON_SCALAR FP64
#endif

#ifndef POLYCON_DIM
#define POLYCON_DIM 02
#endif

#define CONCAT_5( A, B, C, D, E ) A ## B ## C ## D ## E
#define CC_DT_( NAME, D, T ) CONCAT_5( NAME, _, D, _, T )
#define CC_DT( NAME ) CC_DT_( NAME, POLYCON_DIM, POLYCON_SCALAR )

namespace py = pybind11;
using Scalar = POLYCON_SCALAR;
using Array = py::array_t<Scalar, py::array::c_style>;
using Point = PolyCon<Scalar,POLYCON_DIM>::Point;

#define PolyCon_Py CC_DT( PolyCon_py )

struct PolyCon_py {
    PolyCon_py( Array a_dir, Array a_off, Array b_dir, Array b_off ) : pc(
            Span<Point>{ reinterpret_cast<Point *>( a_dir.mutable_data() ), PI( a_dir.shape( 0 ) ) },
            { a_off.mutable_data(), PI( a_off.shape( 0 ) ) },
            Span<Point>{ reinterpret_cast<Point *>( b_dir.mutable_data() ), PI( b_dir.shape( 0 ) ) },
            { b_off.mutable_data(), PI( b_off.shape( 0 ) ) }
    ) { }

    PolyCon_py( PolyCon<Scalar,POLYCON_DIM> &&pc ) : pc( std::move( pc ) ) {
    }

    void write_vtk( const Str &filename ) {
        VtkOutput vo;
        pc.display_vtk( vo );
        vo.save( filename );
    }

    PolyCon_py legendre_transform() {
        return pc.legendre_transform();
    }

    struct VertexData {
        Scalar height;
        CountOfCutTypes cct;
        Point pos;
    };

    auto edge_data( CtInt<1> ) {
        Vec<Vec<VertexData,2>> res;
        pc.for_each_cell( [&]( Cell<Scalar,POLYCON_DIM> &cell ) {
            cell.for_each_edge( [&]( auto num_cuts, const Vertex<Scalar,POLYCON_DIM> &v0, const Vertex<Scalar,POLYCON_DIM> &v1 ) {
                const Scalar h0 = cell.height( v0.pos );
                const Scalar h1 = cell.height( v1.pos );

                CountOfCutTypes c0, c1;
                cell.add_cut_types( c0, v0, pc.nb_bnds() );
                cell.add_cut_types( c1, v1, pc.nb_bnds() );

                res << Vec<VertexData,2> {
                    VertexData{ h0, c0, v0.pos },
                    VertexData{ h1, c1, v1.pos },
                };
            } );
        } );
        return res;
    }

    auto edge_data( auto ) {
        using NC = Vec<SI,POLYCON_DIM-1>;
        std::map<NC,Vec<VertexData,2>,Less> map;
        pc.for_each_cell( [&]( Cell<Scalar,POLYCON_DIM> &cell ) {
            cell.for_each_edge( [&]( auto num_cuts, const Vertex<Scalar,POLYCON_DIM> &v0, const Vertex<Scalar,POLYCON_DIM> &v1 ) {
                std::sort( num_cuts.begin(), num_cuts.end() );
                map_item( map, num_cuts, [&]() -> Vec<VertexData,2> {
                    const Scalar h0 = cell.height( v0.pos );
                    const Scalar h1 = cell.height( v1.pos );

                    CountOfCutTypes c0, c1;
                    cell.add_cut_types( c0, v0, pc.nb_bnds() );
                    cell.add_cut_types( c1, v1, pc.nb_bnds() );

                    return {
                        VertexData{ h0, c0, v0.pos },
                        VertexData{ h0, c1, v1.pos },
                    };
                } );
            } );
        } );

        Vec<Vec<VertexData,2>> res;
        for( const auto &p : map )
            res << p.second;
        return res;
    }

    Array edge_points() {
        auto map = edge_data( CtInt<POLYCON_DIM>() );

        Vec<PI> shape{ map.size(), 2, POLYCON_DIM + 4 };
        Array res( shape );
        for( PI n = 0; n < map.size(); ++n ) {
            for( PI i = 0; i < 2; ++i ) {
                for( PI d = 0; d < POLYCON_DIM; ++d )
                    res.mutable_at( n, i, d ) = map[ n ][ i ].pos[ d ];
                res.mutable_at( n, i, POLYCON_DIM + 0 ) = map[ n ][ i ].height;
                res.mutable_at( n, i, POLYCON_DIM + 1 ) = map[ n ][ i ].cct.nb_ints;
                res.mutable_at( n, i, POLYCON_DIM + 2 ) = map[ n ][ i ].cct.nb_bnds;
                res.mutable_at( n, i, POLYCON_DIM + 3 ) = map[ n ][ i ].cct.nb_infs;
            }
        }
        return res;
    }

    PI ndim() const {
        return pc.ndim();
    }

    PolyCon_py add_polycon( const PolyCon_py &that ) {
        Vec<Scalar> new_f_offs = pc.f_offs;
        return PolyCon<Scalar,POLYCON_DIM>( pc.f_dirs, new_f_offs, pc.b_dirs, pc.b_offs );
    }

    PolyCon_py add_scalar( Scalar that ) {
        Vec<Scalar> new_f_offs = pc.f_offs - that;
        return PolyCon<Scalar,POLYCON_DIM>( pc.f_dirs, new_f_offs, pc.b_dirs, pc.b_offs );
    }

    PolyCon_py mul_scalar( Scalar that ) {
        Vec<Point>  new_f_dirs = that * pc.f_dirs;
        Vec<Scalar> new_f_offs = that * pc.f_offs;
        return PolyCon<Scalar,POLYCON_DIM>( new_f_dirs, new_f_offs, pc.b_dirs, pc.b_offs );
    }

    PolyCon<POLYCON_SCALAR,POLYCON_DIM> pc;
};

void fill_polycon_module( py::module_ &m, Str name ) {
    py::class_<PolyCon_py>( m, name.c_str(), py::module_local() )
        .def( py::init<Array, Array, Array, Array>() )
        .def( "legendre_transform", &PolyCon_py::legendre_transform )
        .def( "edge_points", &PolyCon_py::edge_points )
        .def( "add_polycon", &PolyCon_py::add_polycon )
        .def( "add_scalar", &PolyCon_py::add_scalar )
        .def( "mul_scalar", &PolyCon_py::mul_scalar )
        .def( "write_vtk", &PolyCon_py::write_vtk )
        .def( "ndim", &PolyCon_py::ndim )
        ;
}
