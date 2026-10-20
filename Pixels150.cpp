/* --------------------------------------------------------
   Pixels150 - Copyright (C) 2026 [Carra]
   SPDX-License-Identifier: GPL-3.0-only
  
   The xBR algorithm used here is based on work
   by Hyllian (Jar​​araca), Arwa Arif and Treeki:
   https://github.com/Treeki/libxbr-standalone
  
   See License.md for license and attributions.
-------------------------------------------------------- */


// ========================================================
//      PROGRAM CONFIGURATION
// ========================================================


// include standard C/C++ headers
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// include external headers
#include <png.h>

// define used namespaces
using namespace std;


// ========================================================
//      IMAGE HANDLING FUNCTIONS
// ========================================================


// type for an RGBA pixel
typedef array< uint8_t, 4 > Pixel;

// define an image as a 2D pixel matrix
struct Image
{
    // raw image data; use a 1D vector here since
    // that is how libpng stores an image's pixels
    unsigned Width = 0, Height = 0;
    vector<Pixel> Pixels;
    
    // constructor from size
    Image( unsigned Width_, unsigned Height_ )
    {
        Width = Width_;
        Height = Height_;
        Pixels.resize( Width * Height );
    }
    
    // reading individual pixels; note that we accept
    // out of image coordinates and clamp them to limits
    const Pixel& get( int x, int y ) const
    {
        x = clamp( x, 0, static_cast<int>(Width)  - 1 );
        y = clamp( y, 0, static_cast<int>(Height) - 1 );
        return Pixels[ static_cast<size_t>(y) * Width + x ];
    }
    
    // setting individual pixels; same note as above
    void set( int x, int y, const Pixel& p )
    {
        x = clamp( x, 0, static_cast<int>(Width)  - 1 );
        y = clamp( y, 0, static_cast<int>(Height) - 1 );
        Pixels[ static_cast<size_t>(y) * Width + x ] = p;
    }
};


// ========================================================
//      LOAD AND SAVE IMAGES IN PNG FORMAT
// ========================================================


Image LoadPNG( const filesystem::path& FilePath )
{
    png_image PNG{};
    PNG.version = PNG_IMAGE_VERSION;
    const auto name = FilePath.string();
    
    if( !png_image_begin_read_from_file( &PNG, name.c_str() ) )
      throw runtime_error( "Cannot read source PNG file " + name );
    
    // setting this before png_image_finish_read ensures
    // that any source format will be converted to RGBA
    PNG.format = PNG_FORMAT_RGBA;
    
    if( PNG.width == 0 || PNG.height == 0
    ||  PNG.width  > static_cast<unsigned>(numeric_limits<int>::max() / 3)
    ||  PNG.height > static_cast<unsigned>(numeric_limits<int>::max() / 3) )
    {
        png_image_free( &PNG );
        throw runtime_error( "PNG dimensions not allowed" );
    }
    
    vector<png_byte> bytes( PNG_IMAGE_SIZE(PNG) );
    
    if( !png_image_finish_read(&PNG, nullptr, bytes.data(), 0, nullptr) )
    {
        const string ErrorMessage = PNG.message;
        png_image_free( &PNG );
        throw runtime_error( "Error reading PNG: " + ErrorMessage );
    }
    
    // copy pixel data to our result image
    Image Result( PNG.width, PNG.height );
    
    for( size_t i = 0; i < Result.Pixels.size(); ++i )
      copy_n( bytes.data() + i * 4, 4, Result.Pixels[i].begin() );
    
    // we can close the file now
    png_image_free( &PNG );
    return Result;
}

// --------------------------------------------------------

void SavePNG( const filesystem::path& FilePath, const Image& IMG )
{
    // create and configure a PNG description structure
    png_image PNG{};
    PNG.version = PNG_IMAGE_VERSION;
    PNG.width = IMG.Width;
    PNG.height = IMG.Height;
    PNG.format = PNG_FORMAT_RGBA;
    
    // copy the source image's pixels
    vector<png_byte> bytes( IMG.Pixels.size() * 4 );
    
    for( size_t i = 0; i < IMG.Pixels.size(); ++i )
      copy_n( IMG.Pixels[i].begin(), 4, bytes.data() + i * 4 );
    
    // save to file
    const auto name = FilePath.string();
    
    if( !png_image_write_to_file(&PNG, name.c_str(), 0, bytes.data(), 0, nullptr) )
      throw runtime_error( "Error saving PNG: " + string(PNG.message) );
}


// ========================================================
//      IMAGE RESIZE ALGORITHMS
// ========================================================


// helper function for XBR; it calculates an
// approximate color distance in YUV + alpha
unsigned ColorDifference( const Pixel& a, const Pixel& b )
{
    // full transparency is always considered equal
    if( a[3] == 0 && b[3] == 0 ) return 0;
    
    // component differences in RGB space
    const int dr = (int)a[0] - b[0];
    const int dg = (int)a[1] - b[1];
    const int db = (int)a[2] - b[2];
    
    // component differences in YUV space ()
    const int dy = ( 77 * dr + 150 * dg +  29 * db) / 256;
    const int du = (-43 * dr -  85 * dg + 128 * db) / 256;
    const int dv = (128 * dr - 107 * dg -  21 * db) / 256;
    
    auto difference = abs(dy) + abs(du) + abs(dv) + 2 * abs(int(a[3]) - b[3]);
    return static_cast<unsigned>(difference);
}

// --------------------------------------------------------

// helper function for XBR; defines a similarity threshold
bool ColorsAreSimilar( const Pixel& a, const Pixel& b )
{
    return ColorDifference( a, b ) < 155;
}

// --------------------------------------------------------

// first upscaling stage: this is a compact implementation of
// xBR 3x algorithm with no color blending; this algorithm
// applies some rules over the colors of neighboring pixels to
// decide where to create edges in the upscaled image
Image XBR_3X_NoBlend( const Image& Original )
{
    Image Result( Original.Width * 3, Original.Height * 3 );
    
    // 2D loop for each pixel in the original image
    for( int y = 0; y < static_cast<int>(Original.Height); ++y )
    {
        for( int x = 0; x < static_cast<int>(Original.Width); ++x )
        {
            // init the scaled 3x3 block with the same original pixel
            array< Pixel, 9 > tile;
            tile.fill( Original.get(x, y) );
            
            // process the block 4 times, doing 90-degree rotations;
            // each rotation processes a different corner of the 3x3 block
            for( int rotation = 0; rotation < 4; ++rotation )
            {
                // lambda for rotation process
                auto turn = [rotation](int u, int v)
                {
                    for( int i = 0; i < rotation; ++i )
                    {
                        const int old_u = u;
                        u = -v;
                        v = old_u;
                    }
                    
                    return pair<int, int>{u, v};
                };
                
                // lambdas to read and write a pixel
                auto get = [&]( int u, int v ) -> const Pixel&
                {
                    const auto [du, dv] = turn(u, v);
                    return Original.get(x + du, y + dv);
                };
                
                auto put = [&]( int u, int v, const Pixel& c )
                {
                    const auto [du, dv] = turn(u - 1, v - 1);
                    tile[ static_cast<size_t>(dv + 1) * 3 + (du + 1) ] = c;
                };
                
                // pre-read the 9 pixels (3x3 square) for the rules below
                const auto& E = get(0, 0);
                const auto& F = get(1, 0);
                const auto& H = get(0, 1);
                const auto& I = get(1, 1);
                const auto& C = get(1, -1);
                const auto& G = get(-1, 1);
                const auto& D = get(-1, 0);
                const auto& B = get(0, -1);
                
                // apply the rules for scaling
                if( E == H || E == F ) continue;
                
                const unsigned e = ColorDifference(E, C) + ColorDifference(E, G)
                    + ColorDifference(I, get(0, 2)) + ColorDifference(I, get(2, 0))
                    + 4 * ColorDifference(H, F);
                    
                const unsigned i = ColorDifference(H, D) + ColorDifference(H, get(1, 2))
                    + ColorDifference(F, get(2, 1)) + ColorDifference(F, B)
                    + 4 * ColorDifference(E, I);
                    
                if( e > i ) continue;
                
                const Pixel& SelectedPixel = ColorDifference(E, F) <= ColorDifference(E, H) ? F : H;
                const bool Strong = e < i &&
                    ((!ColorsAreSimilar(F, B) && !ColorsAreSimilar(F, C)) ||
                     (!ColorsAreSimilar(H, D) && !ColorsAreSimilar(H, G)) ||
                     (ColorsAreSimilar(E, I) &&
                      ((!ColorsAreSimilar(F, get(2, 0)) && !ColorsAreSimilar(F, get(2, 1))) ||
                       (!ColorsAreSimilar(H, get(0, 2)) && !ColorsAreSimilar(H, get(1, 2))))) ||
                     ColorsAreSimilar(E, G) || ColorsAreSimilar(E, C));
                
                // bottom-right corner (2,2) and horizontal/vertical extension
                put( 2, 2, SelectedPixel );
                
                if( Strong )
                {
                    const unsigned ke = ColorDifference( F, G );
                    const unsigned ki = ColorDifference( H, C );
                    const bool Horizontal = 2 * ke <= ki && E != G && D != G;
                    const bool Vertical = ke >= 2 * ki && E != C && B != C;
                    
                    if( Horizontal )
                    {
                        put( 1, 2, SelectedPixel );
                        if( Vertical ) put( 0, 2, SelectedPixel );
                    }
                    
                    if( Vertical )
                    {
                        put( 2, 1, SelectedPixel );
                        if( Horizontal ) put( 2, 0, SelectedPixel );
                    }
                }
            }
            
            // store the resulting 3x3 block pixels in the result image
            for( int j = 0; j < 3; ++j )
              for( int k = 0; k < 3; ++k )
                Result.set( x*3 + k, y*3 + j, tile[ j * 3 + k ] );
        }
    }
    
    return Result;
}

// --------------------------------------------------------

// second scaling stage: a very simple nearest-neighbor
// reduction, specialized for the particular case of 50%;
// it simply picks pixels from every other row and column
Image NearestNeighbor_50Percent( const Image& Original )
{
    // create empty image with the final dimensions;
    // for odd dimensions it will round up to add 1 pixel
    Image Result( (Original.Width + 1) / 2, (Original.Height + 1) / 2 );
    
    // skip half of rows and columns for a reduction of 50%
    for( unsigned y = 0; y < Result.Height; ++y )
      for( unsigned x = 0; x < Result.Width; ++x )
        Result.set( x, y, Original.get( x * 2, y * 2 ) );

    return Result;
}


// ========================================================
//      MAIN FUNCTION
// ========================================================


int main( int NumberOfArguments, char** Arguments )
{
    // print usage if needed
    if( NumberOfArguments != 3 )
    {
        cerr << "Usage: Pixels150 <input.png> <output.png>\n";
        return 2;
    }
    
    try
    {
        // read program arguments
        const auto InputPath  = filesystem::path( Arguments[ 1 ] );
        const auto OutputPath = filesystem::path( Arguments[ 2 ] );
        
        // load input PNG image
        const Image Image1X = LoadPNG( InputPath );
        
        // first stage: enlarge to 3X
        const Image Image3X = XBR_3X_NoBlend( Image1X );
        
        // second stage: reduce by half to 1.5X
        const Image FinalImage = NearestNeighbor_50Percent( Image3X );
        
        // save converted PNG file
        SavePNG( OutputPath, FinalImage );
        return 0;
    }
    catch( const exception& e )
    {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
