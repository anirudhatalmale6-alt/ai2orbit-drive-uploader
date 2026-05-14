package com.ai2orbit.driveuploader

import android.content.Context

enum class CloudProvider {
    GOOGLE_DRIVE,
    AMAZON_S3,
    AZURE_BLOB
}

object CloudConfigManager {
    private const val PREF = "ai2orbit_cloud"

    fun getSelectedProvider(ctx: Context): CloudProvider {
        val sp = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        return try {
            CloudProvider.valueOf(sp.getString("provider", "GOOGLE_DRIVE") ?: "GOOGLE_DRIVE")
        } catch (_: Exception) {
            CloudProvider.GOOGLE_DRIVE
        }
    }

    fun setSelectedProvider(ctx: Context, provider: CloudProvider) {
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit()
            .putString("provider", provider.name)
            .apply()
    }

    fun saveS3Config(ctx: Context, config: S3Config) {
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit()
            .putString("s3_access", config.accessKey)
            .putString("s3_secret", config.secretKey)
            .putString("s3_bucket", config.bucket)
            .putString("s3_region", config.region)
            .putString("s3_prefix", config.prefix)
            .apply()
    }

    fun loadS3Config(ctx: Context): S3Config? {
        val sp = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        val access = sp.getString("s3_access", null) ?: return null
        val secret = sp.getString("s3_secret", null) ?: return null
        return S3Config(
            accessKey = access,
            secretKey = secret,
            bucket = sp.getString("s3_bucket", "ai2orbit-photos") ?: "ai2orbit-photos",
            region = sp.getString("s3_region", "us-east-1") ?: "us-east-1",
            prefix = sp.getString("s3_prefix", "AI2ORBIT_Photos/") ?: "AI2ORBIT_Photos/"
        )
    }

    fun saveAzureConfig(ctx: Context, config: AzureConfig) {
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit()
            .putString("az_account", config.accountName)
            .putString("az_key", config.accountKey)
            .putString("az_container", config.container)
            .apply()
    }

    fun loadAzureConfig(ctx: Context): AzureConfig? {
        val sp = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
        val account = sp.getString("az_account", null) ?: return null
        val key = sp.getString("az_key", null) ?: return null
        return AzureConfig(
            accountName = account,
            accountKey = key,
            container = sp.getString("az_container", "ai2orbit-photos") ?: "ai2orbit-photos"
        )
    }

    fun isConfigured(ctx: Context, provider: CloudProvider): Boolean {
        return when (provider) {
            CloudProvider.GOOGLE_DRIVE -> GoogleAuthHelper.isSignedIn(ctx)
            CloudProvider.AMAZON_S3 -> loadS3Config(ctx) != null
            CloudProvider.AZURE_BLOB -> loadAzureConfig(ctx) != null
        }
    }

    fun getProviderLabel(provider: CloudProvider): String {
        return when (provider) {
            CloudProvider.GOOGLE_DRIVE -> "Google Drive"
            CloudProvider.AMAZON_S3 -> "Amazon S3"
            CloudProvider.AZURE_BLOB -> "Azure Blob"
        }
    }
}
