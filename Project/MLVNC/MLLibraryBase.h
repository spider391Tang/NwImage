#ifndef MLLibrary_MLLibraryBase_h
#define MLLibrary_MLLibraryBase_h


namespace MLLibrary {


    /** 
     *  MLLibraryBase provide a basic interface for MLLibrary modules to be follow.
     *  1. int pwrp()
     *  2. int init()
     *  3. int pwrdn()
     *  @author garmin
     *  @version 1.0
     */
class MLLibraryBase {

 public:

    virtual void pwrp();

    virtual void init();

    virtual void pwrdn();
};

} /* End of namespace MLLibrary */

#endif // MLLibrary_MLLibraryBase_h
