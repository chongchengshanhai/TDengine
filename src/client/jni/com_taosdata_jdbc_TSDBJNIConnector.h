/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_taosdata_jdbc_TSDBJNIConnector */

#ifndef _Included_com_taosdata_jdbc_TSDBJNIConnector
#define _Included_com_taosdata_jdbc_TSDBJNIConnector
#ifdef __cplusplus
extern "C" {
#endif
#undef com_taosdata_jdbc_TSDBJNIConnector_INVALID_CONNECTION_POINTER_VALUE
#define com_taosdata_jdbc_TSDBJNIConnector_INVALID_CONNECTION_POINTER_VALUE 0LL
/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_setAllocModeImp
  (JNIEnv *, jclass, jint, jstring, jboolean);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT void JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_dumpMemoryLeakImp
  (JNIEnv *, jclass);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    initImp
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_initImp
  (JNIEnv *, jclass, jstring);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    setOptions
 * Signature: (ILjava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_setOptions
  (JNIEnv *, jclass, jint, jstring);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    getTsCharset
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_getTsCharset
  (JNIEnv *, jclass);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    connectImp
 * Signature: (Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_connectImp
  (JNIEnv *, jobject, jstring, jint, jstring, jstring, jstring);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    executeQueryImp
 * Signature: ([BJ)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_executeQueryImp
  (JNIEnv *, jobject, jbyteArray, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    getErrCodeImp
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_getErrCodeImp
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    getErrMsgImp
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_getErrMsgImp
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    getResultSetImp
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_getResultSetImp
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    freeResultSetImp
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_freeResultSetImp
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    getAffectedRowsImp
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_getAffectedRowsImp
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    getSchemaMetaDataImp
 * Signature: (JJLjava/util/List;)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_getSchemaMetaDataImp
  (JNIEnv *, jobject, jlong, jlong, jobject);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    fetchRowImp
 * Signature: (JJLcom/taosdata/jdbc/TSDBResultSetRowData;)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_fetchRowImp
  (JNIEnv *, jobject, jlong, jlong, jobject);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    closeConnectionImp
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_closeConnectionImp
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    subscribeImp
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JI)J
 */
JNIEXPORT jlong JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_subscribeImp
  (JNIEnv *, jobject, jlong, jboolean, jstring, jstring, jint);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    consumeImp
 * Signature: (J)Lcom/taosdata/jdbc/TSDBResultSetRowData;
 */
JNIEXPORT jobject JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_consumeImp
  (JNIEnv *, jobject, jlong);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    unsubscribeImp
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_unsubscribeImp
  (JNIEnv *, jobject, jlong, jboolean);

/*
 * Class:     com_taosdata_jdbc_TSDBJNIConnector
 * Method:    validateCreateTableSqlImp
 * Signature: (J[B)I
 */
JNIEXPORT jint JNICALL Java_com_taosdata_jdbc_TSDBJNIConnector_validateCreateTableSqlImp
  (JNIEnv *, jobject, jlong, jbyteArray);

#ifdef __cplusplus
}
#endif
#endif
