/*
 * Copyright (C) 2018 ~ 2028 Uniontech Technology Co., Ltd.
 *
 * Author:     chenjun <chenjun@uniontech.com>
 *
 * Maintainer: chenjun <chenjun@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LauncherUnitTest_H
#define LauncherUnitTest_H

#include <QObject>
#include <gtest/gtest.h>

class LauncherUnitTest : public QObject, public testing::Test
{
    Q_OBJECT
public:
    LauncherUnitTest() {}
    virtual ~LauncherUnitTest() {}

    virtual void SetUp() {}
    virtual void TearDown() {}
};

#endif // LauncherUnitTest_H
