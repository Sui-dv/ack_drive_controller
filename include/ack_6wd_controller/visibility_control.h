#ifndef ACK_6WD_CONTROLLER__VISIBILITY_CONTROL_H_
#define ACK_6WD_CONTROLLER__VISIBILITY_CONTROL_H_

// This logic was borrowed (then namespaced) from the examples on the gcc wiki:
//     https://gcc.gnu.org/wiki/Visibility

#if defined _WIN32 || defined __CYGWIN__
#ifdef __GNUC__
#define ACK_6WD_CONTROLLER_EXPORT __attribute__((dllexport))
#define ACK_6WD_CONTROLLER_IMPORT __attribute__((dllimport))
#else
#define ACK_6WD_CONTROLLER_EXPORT __declspec(dllexport)
#define ACK_6WD_CONTROLLER_IMPORT __declspec(dllimport)
#endif
#ifdef ACK_6WD_CONTROLLER_BUILDING_DLL
#define ACK_6WD_CONTROLLER_PUBLIC ACK_6WD_CONTROLLER_EXPORT
#else
#define ACK_6WD_CONTROLLER_PUBLIC ACK_6WD_CONTROLLER_IMPORT
#endif
#define ACK_6WD_CONTROLLER_PUBLIC_TYPE ACK_6WD_CONTROLLER_PUBLIC
#define ACK_6WD_CONTROLLER_LOCAL
#else
#define ACK_6WD_CONTROLLER_EXPORT __attribute__((visibility("default")))
#define ACK_6WD_CONTROLLER_IMPORT
#if __GNUC__ >= 4
#define ACK_6WD_CONTROLLER_PUBLIC __attribute__((visibility("default")))
#define ACK_6WD_CONTROLLER_LOCAL __attribute__((visibility("hidden")))
#else
#define ACK_6WD_CONTROLLER_PUBLIC
#define ACK_6WD_CONTROLLER_LOCAL
#endif
#define ACK_6WD_CONTROLLER_PUBLIC_TYPE
#endif

#endif  // ACK_6WD_CONTROLLER__VISIBILITY_CONTROL_H_
