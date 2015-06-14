#ifndef __OPENCV_DNN_DICT_HPP__
#define __OPENCV_DNN_DICT_HPP__

#include <opencv2/core.hpp>

namespace cv
{
namespace dnn
{

struct DictValue
{
    int type;

    union
    {
        int i;
        unsigned u;
        double d;
        bool b;
        String *s;
    };

    DictValue(const DictValue &r);
    DictValue(int p = 0)        : type(cv::Param::INT), i(p) {}
    DictValue(unsigned p)       : type(cv::Param::UNSIGNED_INT), u(p) {}
    DictValue(double p)         : type(cv::Param::REAL), d(p) {}
    DictValue(bool p)           : type(cv::Param::BOOLEAN), b(p) {}
    DictValue(const String &p)  : type(cv::Param::STRING), s(new String(p)) {}

    template<typename T>
    T get() const;

    bool isString() const;
    bool isInt() const;

    DictValue &operator=(const DictValue &r);

    ~DictValue();

private:
    void release();
};

class Dict
{
    //TODO: maybe this mechanism was realized somewhere in OpenCV?
    typedef std::map<String, DictValue> _Dict;
    _Dict dict;

public:

    bool has(const String &name)
    {
        return dict.count(name);
    }

    DictValue *ptr(const String &name)
    {
        _Dict::iterator i = dict.find(name);
        return (i == dict.end()) ? NULL : &i->second;
    }

    template <typename T>
    const T &get(const String &name) const
    {
        _Dict::const_iterator i = dict.find(name);
        CV_Assert(i != dict.end());
        return i->second.get<T>();
    }

    template <typename T>
    const T &get(const String &name, const T &default_value) const
    {
        _Dict::const_iterator i = dict.find(name);

        if (i != dict.end())
            return i->second.get<T>();
        else
            return default_value;
    }

    template<typename T>
    const T &set(const String &name, const T &value)
    {
        _Dict::iterator i = dict.find(name);

        if (i != dict.end())
            i->second = DictValue(value);
        else
            dict.insert(std::make_pair(name, DictValue(value)));

        return value;
    }
};


template<>
inline int DictValue::get<int>() const
{
    CV_Assert(type == cv::ParamType<int>::type || type == cv::ParamType<unsigned>::type && (int)u >= 0);
    return i;
}

template<>
inline unsigned DictValue::get<unsigned>() const
{
    CV_Assert(type == cv::ParamType<unsigned>::type || type == cv::ParamType<int>::type && i >= 0);
    return u;
}

template<>
inline double DictValue::get<double>() const
{
    CV_Assert(type == cv::ParamType<double>::type);
    return d;
}

template<>
inline float DictValue::get<float>() const
{
    CV_Assert(type == cv::ParamType<double>::type);
    return (float)d;
}

template<>
inline bool DictValue::get<bool>() const
{
    if (type == cv::ParamType<bool>::type)
    {
        return b;
    }
    else if (type == cv::ParamType<int>::type || type == cv::ParamType<unsigned>::type)
    {
        return i;
    }
    else
    {
        CV_Assert(type == cv::ParamType<bool>::type || type == cv::ParamType<int>::type || type == cv::ParamType<unsigned>::type);
        return 0;
    }
}

template<>
inline String DictValue::get<String>() const
{
    CV_Assert(type == cv::ParamType<String>::type);
    return *s;
}

inline void DictValue::release()
{
    if (type == cv::Param::STRING && s != NULL)
    {
        delete s;
        s = NULL;
    }

}

inline DictValue::~DictValue()
{
    release();
}

inline DictValue & DictValue::operator=(const DictValue &r)
{
    if (&r == this)
        return *this;

    release();

    //how to copy anonymous union without memcpy?
    for (size_t i = 0; i < sizeof(*this); i++)
        ((uchar*)this)[i] = ((uchar*)&r)[i];

    if (r.type == cv::Param::STRING)
    {
        s = new String(*r.s);
    }

    return *this;
}

inline DictValue::DictValue(const DictValue &r)
{
    *this = r;
}

inline bool DictValue::isString() const
{
    return (type == cv::Param::STRING);
}

inline bool DictValue::isInt() const
{
    return (type == cv::Param::INT);
}

}
}

#endif