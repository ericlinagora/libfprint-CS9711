// SIGFM algorithm for libfprint

// Copyright (C) 2022 Matthieu CHARETTE <matthieu.charette@gmail.com>
// Copyright (c) 2022 Natasha England-Elbro <natasha@natashaee.me>
// Copyright (c) 2022 Timur Mangliev <tigrmango@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned char SfmPix;
/**
 * @brief Contains information used by the sigfm algorithm for matching
 * @details Get one from sfm_extract() and make sure to clean it up with sfm_free_info()
 * @struct SfmImgInfo
 */
typedef struct SfmImgInfo SfmImgInfo;

/**
 * @brief Extracts information from an image for later use sfm_match_score
 *
 * @param pix Pixels of the image must be width * height in length
 * @param width Width of the image
 * @param height Height of the image
 * @return SfmImgInfo* Info that can be used with the API
 */
SfmImgInfo* sfm_extract(const SfmPix* pix, int width, int height);

/**
 * @brief Destroy an SfmImgInfo
 * @warning Call this instead of free() or you will get UB!
 * @param info SfmImgInfo to destroy
 */
void sfm_free_info(SfmImgInfo* info);

/**
 * @brief Score how closely a frame matches another
 *
 * @param frame Print to be checked
 * @param enrolled Canonical print to verify against
 * @return int Score of how closely they match, values <0 indicate error, 0 means always reject
 */
int sfm_match_score(SfmImgInfo* frame, SfmImgInfo* enrolled);

/**
 * @brief Serialize an image info for storage
 *
 * @param info SfmImgInfo to store
 * @param outlen output: Length of the returned byte array
 * @return unsigned* char byte array for storage, should be free'd by the callee
 */
unsigned char* sfm_serialize_binary(SfmImgInfo* info, int* outlen);
/**
 * @brief Deserialize an SfmImgInfo from storage
 *
 * @param bytes Byte array to deserialize from
 * @param len Length of the byte array
 * @return SfmImgInfo* Deserialized info, or NULL if deserialization failed
 */
SfmImgInfo* sfm_deserialize_binary(const unsigned char* bytes, int len);

/**
 * @brief Keypoints for an image. Low keypoints generally means the image is
 * low quality for matching
 *
 * @param info
 * @return int
 */

int sfm_keypoints_count(SfmImgInfo* info);

/**
 * @brief Copy an SfmImgInfo
 *
 * @param info Source of copy
 * @return SfmImgInfo* Newly allocated and copied version of info
 */
SfmImgInfo* sfm_copy_info(SfmImgInfo* info);

#ifdef __cplusplus
}
#endif
