// SIGFM algorithm for libfprint

// Copyright (C) 2022 Matthieu CHARETTE <matthieu.charette@gmail.com>
// Copyright (c) 2022 Natasha England-Elbro <natasha@natashaee.me>
// Copyright (c) 2022 Timur Mangliev <tigrmango@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//

#pragma once

#include <opencv2/core.hpp>
#include <vector>

struct SigfmImgInfo {
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
};