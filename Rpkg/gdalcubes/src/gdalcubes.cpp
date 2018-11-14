
#include <gdalcubes.h>

// [[Rcpp::plugins("cpp11")]]
// [[Rcpp::depends(RcppProgress)]]
#include <Rcpp.h>
#include <progress.hpp>
#include <progress_bar.hpp>
#include <memory>

using namespace Rcpp;


struct error_handling_r {
  static std::mutex _m_errhandl;
  static std::stringstream _err_stream;
  static bool _defer;
  
  static void defer_output() {
    _m_errhandl.lock();
    _defer = true;
    _m_errhandl.unlock();
  }
  
  static void do_output() {
    _m_errhandl.lock();
    _defer = false;
    Rcpp::Rcout << _err_stream.str() << std::endl;
    _err_stream.str(""); 
    _m_errhandl.unlock();
  }
  
  static void debug(error_level type, std::string msg, std::string where, int error_code) {
    _m_errhandl.lock();
    std::string code = (error_code != 0) ? " (" + std::to_string(error_code) + ")" : "";
    std::string where_str = (where.empty()) ? "" : " [in " + where + "]";
    if (type == 2 || type == 1) {
      _err_stream << "ERROR" << code << ": " << msg << where_str << std::endl;
    } else if (type == 3) {
      _err_stream << "WARNING" << code << ": " << msg << where_str << std::endl;
    } else if (type == 4) {
      _err_stream << "INFO" << code << ": " << msg << where_str << std::endl;
    } else if (type == 5) {
      _err_stream << "DEBUG" << code << ": " << msg << where_str << std::endl;
    }
    if (!_defer) {
      Rcpp::Rcout << _err_stream.str() << std::endl;
      _err_stream.str(""); 
    }
    _m_errhandl.unlock();
  }
};
std::mutex error_handling_r::_m_errhandl;
std::stringstream error_handling_r::_err_stream;
bool error_handling_r::_defer = false;



struct progress_simple_R : public progress {
  std::shared_ptr<progress> get() override { return std::make_shared<progress_simple_R>(); }
  

  
  void set(double p) override {
    _m.lock();
    _set(p);
    _m.unlock();
  };
  
  void increment(double dp) override {
    _m.lock();
    _set(_p + dp);
    _m.unlock();
  }
  virtual void finalize() override {
    _m.lock();
    _rp->update(100);
    error_handling_r::do_output();
    _m.unlock();
  }


  progress_simple_R() : _p(0), _rp(nullptr) {}

  ~progress_simple_R(){
    if (_rp) {
      delete _rp;
    }
  }
  
 
  
  

private:
  
  std::mutex _m;
  double _p;
  Progress *_rp;
  
  void _set(double p) {
    //Rcpp::checkUserInterrupt();
    // if (Progress::check_abort()) {
    //   throw std::string("Operation has been interrupted by user");
    // }
    if (!_rp) {
      error_handling_r::defer_output();
      _rp = new Progress(100,true);
    }
    double p_old = _p;
    _p = p;
    _rp->update((int)(_p*100));
  }
};

// see https://stackoverflow.com/questions/26666614/how-do-i-check-if-an-externalptr-is-null-from-within-r
// [[Rcpp::export]]
Rcpp::LogicalVector libgdalcubes_is_null(SEXP pointer) {
  return Rcpp::LogicalVector(!R_ExternalPtrAddr(pointer));
}


// [[Rcpp::export]]
Rcpp::List libgdalcubes_version() {
  version_info v = config::instance()->get_version_info();
  return(Rcpp::List::create(
      Rcpp::Named("VERSION_MAJOR") = v.VERSION_MAJOR ,
      Rcpp::Named("VERSION_MINOR") = v.VERSION_MINOR ,
      Rcpp::Named("VERSION_PATCH") = v.VERSION_PATCH ,
      Rcpp::Named("BUILD_DATE") = v.BUILD_DATE,
      Rcpp::Named("BUILD_TIME") = v.BUILD_TIME,
      Rcpp::Named("GIT_DESC") = v.GIT_DESC,
      Rcpp::Named("GIT_COMMIT") = v.GIT_COMMIT));
}


// [[Rcpp::export]]
void libgdalcubes_init() {
  config::instance()->gdalcubes_init();
  config::instance()->set_default_progress_bar(std::make_shared<progress_simple_R>());
  //config::instance()->set_default_progress_bar(std::make_shared<progress_none>());
  
  config::instance()->set_error_handler(error_handling_r::debug); // TODO: make configurable
}

// [[Rcpp::export]]
void libgdalcubes_cleanup() {
  config::instance()->gdalcubes_cleanup();
}

// [[Rcpp::export]]
Rcpp::List libgdalcubes_cube_info( SEXP pin) {

  Rcpp::XPtr<std::shared_ptr<cube>> aa = Rcpp::as<Rcpp::XPtr<std::shared_ptr<cube>>>(pin);
  
  std::shared_ptr<cube> x = *aa;
  
  Rcpp::CharacterVector d_name(3);
  Rcpp::NumericVector d_low(3);
  Rcpp::NumericVector d_high(3);
  Rcpp::IntegerVector d_n(3);
  Rcpp::IntegerVector d_chunk(3);

  d_name[0] = "t";
  d_low[0] = x->st_reference()->t0().to_double();
  d_high[0] = x->st_reference()->t1().to_double();
  d_n[0] = x->st_reference()->nt();
  d_chunk[0] = x->chunk_size()[0];

  d_name[1] = "y";
  d_low[1] = x->st_reference()->bottom();
  d_high[1] = x->st_reference()->top();
  d_n[1] = x->st_reference()->ny();
  d_chunk[1] = x->chunk_size()[1];

  d_name[2] = "x";
  d_low[2] = x->st_reference()->left();
  d_high[2] = x->st_reference()->right();
  d_n[2] = x->st_reference()->nx();
  d_chunk[2] = x->chunk_size()[2];

  Rcpp::DataFrame dims =
    Rcpp::DataFrame::create(Rcpp::Named("name")=d_name,
                            Rcpp::Named("low")=d_low,
                            Rcpp::Named("high")=d_high,
                            Rcpp::Named("size")=d_n,
                            Rcpp::Named("chunk_size")=d_chunk);

  Rcpp::CharacterVector b_names(x->bands().count(), "");
  Rcpp::CharacterVector b_type(x->bands().count(), "");
  Rcpp::NumericVector b_offset(x->bands().count(), NA_REAL);
  Rcpp::NumericVector b_scale(x->bands().count(), NA_REAL);
  Rcpp::NumericVector b_nodata(x->bands().count(), NA_REAL);
  Rcpp::CharacterVector b_unit(x->bands().count(), "");

  for (uint16_t i=0; i<x->bands().count(); ++i) {
    b_names[i] = x->bands().get(i).name;
    b_type[i] = x->bands().get(i).type;
    b_offset[i] = x->bands().get(i).offset;
    b_scale[i] = x->bands().get(i).scale;
    b_nodata[i] = (x->bands().get(i).no_data_value.empty())? NA_REAL : std::stod(x->bands().get(i).no_data_value);
    b_unit[i] = x->bands().get(i).unit;
  }

  Rcpp::DataFrame bands =
    Rcpp::DataFrame::create(Rcpp::Named("name")=b_names,
                            Rcpp::Named("type")=b_type,
                            Rcpp::Named("offset")=b_offset,
                            Rcpp::Named("scale")=b_scale,
                            Rcpp::Named("nodata")=b_nodata,
                            Rcpp::Named("unit")=b_unit);

  return Rcpp::List::create(Rcpp::Named("bands") = bands,
                            Rcpp::Named("dimensions") = dims,
                            Rcpp::Named("proj") = x->st_reference()->proj(),
                            Rcpp::Named("graph") = x->make_constructible_json().dump(2),
                            Rcpp::Named("size") = Rcpp::IntegerVector::create(x->size()[0], x->size()[1], x->size()[2], x->size()[3]));

}


// [[Rcpp::export]]
SEXP libgdalcubes_open_image_collection(std::string filename) {
  std::shared_ptr<image_collection>* x = new std::shared_ptr<image_collection>( std::make_shared<image_collection>(filename));
  Rcout << (*x)->to_string() << std::endl;

  Rcpp::XPtr< std::shared_ptr<image_collection> > p(x, true) ;
  return p;
}

// [[Rcpp::export]]
SEXP libgdalcubes_create_image_collection_cube(std::string filename, SEXP v = R_NilValue) {

  try {
    std::shared_ptr<image_collection_cube>* x = new  std::shared_ptr<image_collection_cube>(std::make_shared<image_collection_cube>(filename));
    
    
    if (v != R_NilValue) {
      Rcpp::List view = Rcpp::as<Rcpp::List>(v);
      if (Rcpp::as<Rcpp::List>(view["space"])["right"] != R_NilValue) {
        (*x)->st_reference()->right() = Rcpp::as<Rcpp::List>(view["space"])["right"];
      }
      if (Rcpp::as<Rcpp::List>(view["space"])["left"] != R_NilValue) {
        (*x)->st_reference()->left() = Rcpp::as<Rcpp::List>(view["space"])["left"];
      }
      if (Rcpp::as<Rcpp::List>(view["space"])["top"] != R_NilValue) {
        (*x)->st_reference()->top() = Rcpp::as<Rcpp::List>(view["space"])["top"];
      }
      if (Rcpp::as<Rcpp::List>(view["space"])["bottom"] != R_NilValue) {
        (*x)->st_reference()->bottom() = Rcpp::as<Rcpp::List>(view["space"])["bottom"];
      }
      // nx overwrites dx
      if (Rcpp::as<Rcpp::List>(view["space"])["dx"] != R_NilValue) {
        (*x)->st_reference()->dx(Rcpp::as<Rcpp::List>(view["space"])["dx"]);
      }
      if (Rcpp::as<Rcpp::List>(view["space"])["nx"] != R_NilValue) {
        (*x)->st_reference()->nx() = Rcpp::as<Rcpp::List>(view["space"])["nx"];
      }
      // ny overwrites dy
      if (Rcpp::as<Rcpp::List>(view["space"])["dy"] != R_NilValue) {
        (*x)->st_reference()->dy(Rcpp::as<Rcpp::List>(view["space"])["dy"]);
      }
      if (Rcpp::as<Rcpp::List>(view["space"])["ny"] != R_NilValue) {
        (*x)->st_reference()->ny() = Rcpp::as<Rcpp::List>(view["space"])["ny"];
      }
      if (Rcpp::as<Rcpp::List>(view["space"])["proj"] != R_NilValue) {
        (*x)->st_reference()->proj() = Rcpp::as<Rcpp::CharacterVector>(Rcpp::as<Rcpp::List>(view["space"])["proj"])[0];
      }
      if (Rcpp::as<Rcpp::List>(view["time"])["t0"] != R_NilValue) {
        std::string tmp = Rcpp::as<Rcpp::String>(Rcpp::as<Rcpp::List>(view["time"])["t0"]);
        (*x)->st_reference()->t0() = datetime::from_string(tmp);
      }
      if (Rcpp::as<Rcpp::List>(view["time"])["t1"] != R_NilValue) {
        std::string tmp = Rcpp::as<Rcpp::String>(Rcpp::as<Rcpp::List>(view["time"])["t1"]);
        (*x)->st_reference()->t1() = datetime::from_string(tmp);
      }
      
      // dt overwrites nt
      if (Rcpp::as<Rcpp::List>(view["time"])["nt"] != R_NilValue) {
        (*x)->st_reference()->nt(Rcpp::as<Rcpp::List>(view["time"])["nt"]);
      }
      if (Rcpp::as<Rcpp::List>(view["time"])["dt"] != R_NilValue) {
        std::string tmp = Rcpp::as<Rcpp::String>(Rcpp::as<Rcpp::List>(view["time"])["dt"]);
        (*x)->st_reference()->dt() = duration::from_string(tmp);
        (*x)->st_reference()->t0().unit() = (*x)->st_reference()->dt().dt_unit; 
        (*x)->st_reference()->t1().unit() = (*x)->st_reference()->dt().dt_unit; 
      }
      
      
      if (view["aggregation"] != R_NilValue) {
        std::string tmp = Rcpp::as<Rcpp::String>(view["aggregation"]);
        std::dynamic_pointer_cast<cube_view>((*x)->st_reference())->aggregation_method() = aggregation::from_string(tmp);
      }
      if (view["resampling"] != R_NilValue) {
        std::string tmp = Rcpp::as<Rcpp::String>(view["resampling"]);
        std::dynamic_pointer_cast<cube_view>((*x)->st_reference())->resampling_method() = resampling::from_string(tmp);
      }
    }
    
    
    //Rcpp::Rcout << std::dynamic_pointer_cast<cube_view>((*x)->st_reference())->write_json_string() << std::endl;
    
    Rcpp::XPtr< std::shared_ptr<image_collection_cube> > p(x, true) ;
    
    return p;
    
  }
  catch (std::string s) {
    Rcpp::stop(s);
  }
 
  
}


// [[Rcpp::export]]
SEXP libgdalcubes_create_reduce_cube(SEXP pin, std::string reducer) {
  try {
    Rcpp::XPtr< std::shared_ptr<cube> > aa = Rcpp::as<Rcpp::XPtr<std::shared_ptr<cube>>>(pin);
    
    std::shared_ptr<reduce_cube>* x = new std::shared_ptr<reduce_cube>( std::make_shared<reduce_cube>(*aa, reducer));
    Rcpp::XPtr< std::shared_ptr<reduce_cube> > p(x, true) ;
    
    return p;
    
  }
  catch (std::string s) {
    Rcpp::stop(s);
  }
}



// [[Rcpp::export]]
void libgdalcubes_eval_reduce_cube( SEXP pin, std::string outfile, std::string of) {
  try {
    Rcpp::XPtr< std::shared_ptr<reduce_cube> > aa = Rcpp::as<Rcpp::XPtr< std::shared_ptr<reduce_cube> >>(pin);
    (*aa)->write_gdal_image(outfile, of);
  }
  catch (std::string s) {
    Rcpp::stop(s);
  }
}


// [[Rcpp::export]]
SEXP libgdalcubes_create_stream_cube(SEXP pin, std::string cmd, std::vector<int> chunk_size) {
  try {
    Rcpp::XPtr< std::shared_ptr<image_collection_cube> > aa = Rcpp::as<Rcpp::XPtr< std::shared_ptr<image_collection_cube> >>(pin);
    (*aa)->set_chunk_size(chunk_size[0], chunk_size[1], chunk_size[2]); // important: change chunk size before creating the cube
    
    std::shared_ptr<stream_cube>* x = new std::shared_ptr<stream_cube>( std::make_shared<stream_cube>(*aa, cmd));
    
    Rcpp::XPtr< std::shared_ptr<stream_cube> > p(x, true) ;
  
    return p;
  }
  catch (std::string s) {
    Rcpp::stop(s);
  }
}


// [[Rcpp::export]]
void libgdalcubes_set_threads(IntegerVector n) {
  if (n[0] > 1) {
    config::instance()->set_default_chunk_processor(std::dynamic_pointer_cast<chunk_processor>(std::make_shared<chunk_processor_multithread>(n[0])));
  }
  else {
    config::instance()->set_default_chunk_processor(std::dynamic_pointer_cast<chunk_processor>(std::make_shared<chunk_processor_singlethread>()));
  }
}


