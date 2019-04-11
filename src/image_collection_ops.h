/*
   Copyright 2019 Marius Appel <marius.appel@uni-muenster.de>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef IMAGE_COLLECTION_OPS_H
#define IMAGE_COLLECTION_OPS_H

#include "image_collection.h"

namespace gdalcubes {

    /**
     * Batch processing operations over all GDAL datasets of a collection
     */
    class image_collection_ops {



        // gdal_translate
        static void translate(image_collection in, std::string out_filename, std::vector<std::string> gdal_translate_args);

        // gdal_addo
        static void addo(image_collection in, std::vector<std::string> gdaladdo_args);

    };



} // namespace gdalcubes

#endif

