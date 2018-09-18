//------------------------------------------------------------------------------
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// © H2O.ai 2018
//------------------------------------------------------------------------------
#include "datatable.h"
#include "frame/py_frame.h"
#include "python/oiter.h"
#include "utils/assert.h"


//------------------------------------------------------------------------------
// Frame::cbind
//------------------------------------------------------------------------------

namespace py {

// Forward-declare
static void check_nrows(DataTable* dt, int64_t* nrows);
static Error item_error(const py::_obj&);



PKArgs Frame::Type::args_cbind(0, 0, 1, true, false, {"force"});

void Frame::Type::_init_cbind(Methods& mm, GetSetters&)
{
  mm.add<&Frame::cbind, args_cbind>("cbind",
    "cbind(self, *frames, force=False)\n"
    "--\n\n" R"(
Append columns of Frames `frames` to the current Frame.

This is equivalent to `pandas.concat(axis=1)`: the Frames are combined
by columns, i.e. cbinding a Frame of shape [n x m] to a Frame of
shape [n x k] produces a Frame of shape [n x (m + k)].

As a special case, if you cbind a single-row Frame, then that row will
be replicated as many times as there are rows in the current Frame. This
makes it easy to create constant columns, or to append reduction results
(such as min/max/mean/etc) to the current Frame.

If Frame(s) being appended have different number of rows (with the
exception of Frames having 1 row), then the operation will fail by
default. You can force cbinding these Frames anyways by providing option
`force=True`: this will fill all 'short' Frames with NAs. Thus there is
a difference in how Frames with 1 row are treated compared to Frames
with any other number of rows.

Parameters
----------
frames: sequence or list of Frames
    One or more Frame to append. They should have the same number of
    rows (unless option `force` is also used).

force: boolean
    If True, allows Frames to be appended even if they have unequal
    number of rows. The resulting Frame will have number of rows equal
    to the largest among all Frames. Those Frames which have less
    than the largest number of rows, will be padded with NAs (with the
    exception of Frames having just 1 row, which will be replicated
    instead of filling with NAs).
)");
}


void Frame::cbind(const PKArgs& args) {
  bool force = args[0]? args[0].to_bool_strict() : false;

  int64_t nrows = dt->nrows;
  std::vector<DataTable*> dts;
  for (auto va : args.varargs()) {
    if (va.is_frame()) {
      DataTable* idt = va.to_frame();
      if (idt->ncols == 0 || idt->nrows == 0) continue;
      if (!force) check_nrows(idt, &nrows);
      dts.push_back(idt);
    }
    else if (va.is_iterable()) {
      for (auto item : va.to_pyiter()) {
        if (item.is_frame()) {
          DataTable* idt = item.to_frame();
          if (idt->ncols == 0 || idt->nrows == 0) continue;
          if (!force) check_nrows(idt, &nrows);
          dts.push_back(idt);
        } else {
          throw item_error(item);
        }
      }
    } else {
      throw item_error(va);
    }
  }

  _clear_types();
  dt->cbind(dts);
}


static void check_nrows(DataTable* dt, int64_t* nrows) {
  int64_t inrows = dt->nrows;
  if (*nrows <= 1) *nrows = inrows;
  if (*nrows == inrows || inrows == 1) return;
  throw ValueError() << "Cannot merge frame with " << inrows << " rows to "
      "a frame with " << *nrows << " rows. Use `force=True` to disregard "
      "this check and merge the frames anyways.";
}

static Error item_error(const py::_obj& item) {
  return TypeError() << "Frame.cbind() expects a list or sequence of Frames, "
      "but got an argument of type " << item.typeobj();
}


}  // namespace py


//------------------------------------------------------------------------------
// DataTable::cbind
//------------------------------------------------------------------------------

/**
 * Merge datatables `dts` into the datatable `dt`, by columns. Datatable `dt`
 * will be modified in-place. If any datatable has less rows than the others,
 * it will be filled with NAs; with the exception of 1-row datatables which will
 * be expanded to the desired height by duplicating that row.
 */
DataTable* DataTable::cbind(std::vector<DataTable*> dts)
{
  int64_t t_ncols = ncols;
  int64_t t_nrows = nrows;
  for (size_t i = 0; i < dts.size(); ++i) {
    t_ncols += dts[i]->ncols;
    if (t_nrows < dts[i]->nrows) t_nrows = dts[i]->nrows;
  }

  // First, materialize the "main" datatable if it is a view
  // TODO: remove after #1188
  reify();

  // Fix up the main datatable if it has too few rows
  if (nrows < t_nrows) {
    for (int64_t i = 0; i < ncols; ++i) {
      columns[i]->resize_and_fill(t_nrows);
    }
    nrows = t_nrows;
  }

  // Append columns from `dts` into the "main" datatable
  std::vector<std::string> newnames = names;
  columns = dt::arealloc(columns, t_ncols + 1);
  columns[t_ncols] = nullptr;
  int64_t j = ncols;
  for (size_t i = 0; i < dts.size(); ++i) {
    int64_t ncolsi = dts[i]->ncols;
    int64_t nrowsi = dts[i]->nrows;
    for (int64_t ii = 0; ii < ncolsi; ++ii) {
      Column *c = dts[i]->columns[ii]->shallowcopy();
      c->reify();
      if (nrowsi < t_nrows) c->resize_and_fill(t_nrows);
      columns[j++] = c;
    }
    const auto& namesi = dts[i]->names;
    xassert(namesi.size() == static_cast<size_t>(ncolsi));
    newnames.insert(newnames.end(), namesi.begin(), namesi.end());
  }
  xassert(j == t_ncols);

  // Done.
  ncols = t_ncols;
  set_names(newnames);
  return this;
}