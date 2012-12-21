/* Copyright STIFTELSEN SINTEF 2012
 *
 * Authors: Christopher Dyken <christopher.dyken@sintef.no>
 *
 * This file is part of the HPMC library.
 *
 * The HPMC library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License ("GPL") as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * The HPMC library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * the HPMC library.  If not, see <http://www.gnu.org/licenses/>.
 */

// Extracting iso-surfaces from a scalar field stored on disc.
//
// This example demonstrates the most basic use of HPMC, providing the scalar
// field as a 3D texture. The example gets volume dimensions and a file name of
// a 8-bit raw dataset from the command line, reads the data into a 3D texture
// and passes this texture to HPMC.
//
// For each frame, a time-depenent iso-value is calculated, passed to HPMC which
// analyzes the scalar field using this iso-value. Then, HPMC renders the
// corresponding iso-surface. Wireframe rendering is done straight-forwardly by
// rendering the surface twice (traversing the HistoPyramid both times), one
// time with solid triangles in a dark color offset slightly away from the
// camera, and the second time using the line-drawing polygon mode to render
// the actual wireframe in white.


#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <GL/glew.h>
#ifdef __APPLE__
#include <glut.h>
#else
#include <GL/glut.h>
#endif
#include "hpmc.h"
#include "../common/common.hpp"

using std::ifstream;
using std::vector;
using std::string;
using std::cerr;
using std::endl;

int                             volume_size_x       = 0;
int                             volume_size_y       = 0;
int                             volume_size_z       = 0;
float                           iso                 = 0.5f;
GLuint                          volume_tex          = 0;
GLuint                          shaded_p            = 0;
GLint                           shaded_loc_pm       = -1;
GLint                           shaded_loc_nm       = -1;
GLint                           shaded_loc_color    = -1;
GLuint                          flat_p              = 0;
GLint                           flat_loc_pm         = -1;
GLint                           flat_loc_color      = -1;
vector<GLubyte>                 dataset;
struct HPMCConstants*           hpmc_c              = NULL;
struct HPMCIsoSurface*          hpmc_h              = NULL;
struct HPMCIsoSurfaceRenderer*  hpmc_th_flat        = NULL;
struct HPMCIsoSurfaceRenderer*  hpmc_th_shaded      = NULL;

namespace resources {
    extern const std::string    solid_vs_110;
    extern const std::string    solid_vs_130;
    extern const std::string    solid_fs_110;
    extern const std::string    solid_fs_130;
    extern const std::string    phong_vs_110;
    extern const std::string    phong_vs_130;
    extern const std::string    phong_fs_110;
    extern const std::string    phong_fs_130;
}

void
printHelp( const std::string& appname )
{
    cerr << "HPMC demo application that visualizes raw volumes."<<endl<<endl;
    cerr << "Usage: " << appname << " [options] xsize ysize zsize rawfile"<<endl<<endl;
    cerr << "where: xsize    The number of samples in the x-direction."<<endl;
    cerr << "       ysize    The number of samples in the y-direction."<<endl;
    cerr << "       zsize    The number of samples in the z-direction."<<endl;
    cerr << "       rawfile  Filename of a raw set of bytes describing"<<endl;
    cerr << "                the volume."<<endl<<endl;
    printOptions();
    cerr << endl;
    cerr << "Example usage:"<<endl;
    cerr << "    " << appname << " 64 64 64 neghip.raw"<< endl;
    cerr << "    " << appname << " 256 256 256 foot.raw"<< endl;
    cerr << "    " << appname << " 256 256 178 BostonTeapot.raw"<< endl;
    cerr << "    " << appname << " 301 324 56 lobster.raw"<< endl;
    cerr << endl;
    cerr << "Raw volumes can be found e.g. at http://www.volvis.org."<<endl;
}

// -----------------------------------------------------------------------------
void
init( int argc, char **argv )
{
    //
    if( argc != 5 ) {
        printHelp( argv[0] );
        exit( EXIT_FAILURE );
    }
    else {
        volume_size_x = atoi( argv[1] );
        volume_size_y = atoi( argv[2] );
        volume_size_z = atoi( argv[3] );

        dataset.resize( volume_size_x * volume_size_y * volume_size_z );
        ifstream datafile( argv[4], std::ios::in | std::ios::binary );
        if( datafile.good() ) {
            datafile.read( reinterpret_cast<char*>( &dataset[0] ),
                           dataset.size() );
        }
        else {
            cerr << "Error opening \"" << argv[4] << "\" for reading." << endl;
            exit( EXIT_FAILURE );
        }
    }


    // --- upload volume ------------------------------------------------------


    // --- create HistoPyramid -------------------------------------------------
    hpmc_c = HPMCcreateConstants( hpmc_target, hpmc_debug );
    hpmc_h = HPMCcreateIsoSurface( hpmc_c );

    HPMCsetLatticeSize( hpmc_h,
                        volume_size_x,
                        volume_size_y,
                        volume_size_z );

    HPMCsetGridSize( hpmc_h,
                     volume_size_x-1,
                     volume_size_y-1,
                     volume_size_z-1 );

    float max_size = std::max( volume_size_x, std::max( volume_size_y, volume_size_z ) );
    HPMCsetGridExtent( hpmc_h,
                       volume_size_x / max_size,
                       volume_size_y / max_size,
                       volume_size_z / max_size );

    // Setup field
    GLint alignment;
    GLenum channel = (hpmc_target < HPMC_TARGET_GL31_GLSL140 ? GL_ALPHA : GL_RED );
    glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glGenTextures( 1, &volume_tex );
    glBindTexture( GL_TEXTURE_3D, volume_tex );
    glTexImage3D( GL_TEXTURE_3D, 0, channel,
                  volume_size_x, volume_size_y, volume_size_z, 0,
                  channel, GL_UNSIGNED_BYTE, &dataset[0] );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP);
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0 );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0 );
    glBindTexture( GL_TEXTURE_3D, 0 );
    glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
    HPMCsetFieldTexture3D( hpmc_h,
                           volume_tex,
                           channel,
                           GL_NONE );


    // --- create traversal vertex shader --------------------------------------
    const char* sources[2];
    char* traversal_code;

    // Shaded pipeline
    hpmc_th_shaded = HPMCcreateIsoSurfaceRenderer( hpmc_h );

    // Vertex shader
    traversal_code = HPMCisoSurfaceRendererShaderSource( hpmc_th_shaded );

    GLuint shaded_v = glCreateShader( GL_VERTEX_SHADER );
    sources[0] = hpmc_target < HPMC_TARGET_GL30_GLSL130
               ? resources::phong_vs_110.c_str()
               : resources::phong_vs_130.c_str();
    sources[1] = traversal_code;
    glShaderSource( shaded_v, 2, sources, NULL );
    compileShader( shaded_v, "shaded vertex shader" );
    free( traversal_code );

    // Fragment shader
    GLuint shaded_f = glCreateShader( GL_FRAGMENT_SHADER );
    sources[0] = hpmc_target < HPMC_TARGET_GL30_GLSL130
               ? resources::phong_fs_110.c_str()
               : resources::phong_fs_130.c_str();
    glShaderSource( shaded_f, 1, sources, NULL );
    compileShader( shaded_f, "shaded fragment shader" );

    // Program
    shaded_p = glCreateProgram();
    glAttachShader( shaded_p, shaded_v );
    glAttachShader( shaded_p, shaded_f );
    glDeleteShader( shaded_v );
    glDeleteShader( shaded_f );
    if( HPMC_TARGET_GL30_GLSL130 <= hpmc_target ) {
        glBindFragDataLocation( shaded_p, 0, "fragment" );
    }
    linkProgram( shaded_p, "shaded program" );
    shaded_loc_pm = glGetUniformLocation( shaded_p, "PM" );
    shaded_loc_nm = glGetUniformLocation( shaded_p, "NM" );
    shaded_loc_color = glGetUniformLocation( shaded_p, "color" );

    // Associate program with traversal handle
    HPMCsetIsoSurfaceRendererProgram( hpmc_th_shaded,
                                      shaded_p,
                                      0, 1, 2 );

    // Flat-shaded pipeline
    hpmc_th_flat = HPMCcreateIsoSurfaceRenderer( hpmc_h );

    // Vertex shader
    traversal_code = HPMCisoSurfaceRendererShaderSource( hpmc_th_flat );
    GLuint flat_v = glCreateShader( GL_VERTEX_SHADER );
    sources[0] = hpmc_target < HPMC_TARGET_GL30_GLSL130
               ? resources::solid_vs_110.c_str()
               : resources::solid_vs_130.c_str();
    sources[1] = traversal_code;
        glShaderSource( flat_v, 2, sources, NULL );
    compileShader( flat_v, "flat vertex shader" );
    free( traversal_code );

    // Fragment shader
    GLuint flat_f = glCreateShader( GL_FRAGMENT_SHADER );
    sources[0] = hpmc_target < HPMC_TARGET_GL30_GLSL130
               ? resources::solid_fs_110.c_str()
               : resources::solid_fs_130.c_str();
    glShaderSource( flat_f, 1, sources, NULL );

    // Program
    flat_p = glCreateProgram();
    glAttachShader( flat_p, flat_v );
    glAttachShader( flat_p, flat_f );
    glDeleteShader( flat_v );
    glDeleteShader( flat_f );
    if( HPMC_TARGET_GL30_GLSL130 <= hpmc_target ) {
        glBindFragDataLocation( flat_p, 0, "fragment" );
    }
    linkProgram( flat_p, "flat program" );
    flat_loc_pm = glGetUniformLocation( flat_p, "PM" );
    flat_loc_color = glGetUniformLocation( flat_p, "color" );

    // Associate program with traversal handle
    HPMCsetIsoSurfaceRendererProgram( hpmc_th_flat,
                                      flat_p,
                                      0, 1, 2 );

    glPolygonOffset( 1.0, 1.0 );
}

void
render( float t, float dt, float fps, const GLfloat* P, const GLfloat* MV, const GLfloat* PMV, const GLfloat *NM )
{
    // Clear screen
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // Build histopyramid
    iso = sin(t);
    HPMCbuildIsoSurface( hpmc_h, iso );
    // Set up view matrices if pre 3.0
    glEnable( GL_DEPTH_TEST );
    if( hpmc_target < HPMC_TARGET_GL30_GLSL130 ) {
        glMatrixMode( GL_PROJECTION );
        glLoadMatrixf( P );
        glMatrixMode( GL_MODELVIEW );
        glLoadMatrixf( MV );
    }

    if( !wireframe ) {
        // Solid shaded rendering
        glUseProgram( shaded_p );
        if( hpmc_target < HPMC_TARGET_GL30_GLSL130 ) {
            glColor3f( 1.0-iso, 0.0, iso );
        }
        else {
            glUniformMatrix4fv( shaded_loc_pm, 1, GL_FALSE, PMV );
            glUniformMatrix3fv( shaded_loc_nm, 1, GL_FALSE, NM );
            glUniform4f( shaded_loc_color,  1.0-iso, 0.0, iso, 1.f );
        }
        HPMCextractVertices( hpmc_th_shaded, GL_FALSE );
    }
    else {
        // Wireframe rendering
        glUseProgram( flat_p );
        if( hpmc_target < HPMC_TARGET_GL30_GLSL130 ) {
            glColor3f( 0.2*(1.0-iso), 0.0, 0.2*iso );
        }
        else {
            glUniformMatrix4fv( flat_loc_pm, 1, GL_FALSE, PMV );
            glUniform4f( flat_loc_color,  1.0-iso, 0.0, iso, 1.f );
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable( GL_POLYGON_OFFSET_FILL );
        HPMCextractVertices( hpmc_th_flat, GL_FALSE );
        glDisable( GL_POLYGON_OFFSET_FILL );

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE );
        glUseProgram( flat_p );
        if( hpmc_target < HPMC_TARGET_GL30_GLSL130 ) {
            glColor3f( 1.0, 1.0, 1.0 );
        }
        else {
            glUniform4f( flat_loc_color, 1.f, 1.f, 1.f, 1.f );
        }
        HPMCextractVertices( hpmc_th_flat, GL_FALSE );
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

const std::string
infoString( float fps )
{
    std::stringstream o;
    o << std::setprecision(5) << fps << " fps, "
      << volume_size_x << 'x'
      << volume_size_y << 'x'
      << volume_size_z << " samples, "
      << (int)( ((volume_size_x-1)*(volume_size_y-1)*(volume_size_z-1)*fps)/1e6 )
      << " MVPS, "
      << HPMCacquireNumberOfVertices( hpmc_h )/3
      << " vertices, iso=" << iso
      << (wireframe?"[wireframe]":"");
    return o.str();
}
