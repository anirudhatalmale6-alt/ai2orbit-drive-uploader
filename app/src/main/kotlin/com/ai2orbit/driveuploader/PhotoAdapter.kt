package com.ai2orbit.driveuploader

import android.net.Uri
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.CheckBox
import android.widget.ImageView
import androidx.recyclerview.widget.RecyclerView
import com.ai2orbit.driveuploader.R

class PhotoAdapter(
    private val photos: List<Uri>,
    private val selected: MutableSet<Uri>,
    private val onSelectionChanged: () -> Unit
) : RecyclerView.Adapter<PhotoAdapter.VH>() {

    inner class VH(view: View) : RecyclerView.ViewHolder(view) {
        val image: ImageView = view.findViewById(R.id.ivPhoto)
        val check: CheckBox = view.findViewById(R.id.cbSelect)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_photo_grid, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val uri = photos[position]
        holder.image.setImageURI(uri)
        holder.check.isChecked = selected.contains(uri)

        holder.itemView.setOnClickListener {
            if (selected.contains(uri)) {
                selected.remove(uri)
            } else {
                selected.add(uri)
            }
            holder.check.isChecked = selected.contains(uri)
            onSelectionChanged()
        }

        holder.check.setOnClickListener {
            if (holder.check.isChecked) {
                selected.add(uri)
            } else {
                selected.remove(uri)
            }
            onSelectionChanged()
        }
    }

    override fun getItemCount() = photos.size
}
