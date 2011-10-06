/*
 * Copyright (C) 2011 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 *
 */

#ifndef MISC_H
#define	MISC_H
#include <string>

namespace LSM {

/**
 * Retrieve the first value that matches key from a query string
 * @param query_string  Query String to parse.
 * @param key           Key to look for
 * @return empty string if key not found, else returns value.
 */
std::string getValue(std::string query_string, std::string key);



} //End namespace


#endif	/* MISC_H */

