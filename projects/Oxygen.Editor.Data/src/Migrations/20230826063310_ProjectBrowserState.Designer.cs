// <auto-generated />
using System;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;
using Microsoft.EntityFrameworkCore.Storage.ValueConversion;
using Oxygen.Editor.Data;

#nullable disable

namespace Oxygen.Editor.Data.Migrations
{
    [DbContext(typeof(PersistentState))]
    [Migration("20230826063310_ProjectBrowserState")]
    partial class ProjectBrowserState
    {
        /// <inheritdoc />
        protected override void BuildTargetModel(ModelBuilder modelBuilder)
        {
#pragma warning disable 612, 618
            modelBuilder.HasAnnotation("ProductVersion", "7.0.10");

            modelBuilder.Entity("Oxygen.Editor.Models.ProjectBrowserState", b =>
                {
                    b.Property<int>("Id")
                        .ValueGeneratedOnAdd()
                        .HasColumnType("INTEGER");

                    b.Property<string>("LastSaveLocation")
                        .IsRequired()
                        .HasColumnType("TEXT");

                    b.HasKey("Id");

                    b.ToTable("ProjectBrowserStates", t =>
                        {
                            t.HasCheckConstraint("CK_Single_Row", "[Id] = 1");
                        });

                    b.HasData(
                        new
                        {
                            Id = 1,
                            LastSaveLocation = ""
                        });
                });

            modelBuilder.Entity("Oxygen.Editor.Models.RecentlyUsedProject", b =>
                {
                    b.Property<int>("Id")
                        .ValueGeneratedOnAdd()
                        .HasColumnType("INTEGER");

                    b.Property<DateTime>("LastUsedOn")
                        .HasColumnType("TEXT");

                    b.Property<string>("Location")
                        .IsRequired()
                        .HasColumnType("TEXT");

                    b.Property<int>("ProjectBrowserStateId")
                        .HasColumnType("INTEGER");

                    b.HasKey("Id");

                    b.HasIndex("Location");

                    b.HasIndex("ProjectBrowserStateId");

                    b.ToTable("RecentlyUsedProjects");
                });

            modelBuilder.Entity("Oxygen.Editor.Models.RecentlyUsedTemplate", b =>
                {
                    b.Property<int>("Id")
                        .ValueGeneratedOnAdd()
                        .HasColumnType("INTEGER");

                    b.Property<DateTime>("LastUsedOn")
                        .HasColumnType("TEXT");

                    b.Property<string>("Location")
                        .IsRequired()
                        .HasColumnType("TEXT");

                    b.Property<int>("ProjectBrowserStateId")
                        .HasColumnType("INTEGER");

                    b.HasKey("Id");

                    b.HasIndex("Location");

                    b.HasIndex("ProjectBrowserStateId");

                    b.ToTable("RecentlyUsedTemplates");
                });

            modelBuilder.Entity("Oxygen.Editor.Models.RecentlyUsedProject", b =>
                {
                    b.HasOne("Oxygen.Editor.Models.ProjectBrowserState", null)
                        .WithMany("RecentProjects")
                        .HasForeignKey("ProjectBrowserStateId")
                        .OnDelete(DeleteBehavior.Cascade)
                        .IsRequired();
                });

            modelBuilder.Entity("Oxygen.Editor.Models.RecentlyUsedTemplate", b =>
                {
                    b.HasOne("Oxygen.Editor.Models.ProjectBrowserState", null)
                        .WithMany("RecentTemplates")
                        .HasForeignKey("ProjectBrowserStateId")
                        .OnDelete(DeleteBehavior.Cascade)
                        .IsRequired();
                });

            modelBuilder.Entity("Oxygen.Editor.Models.ProjectBrowserState", b =>
                {
                    b.Navigation("RecentProjects");

                    b.Navigation("RecentTemplates");
                });
#pragma warning restore 612, 618
        }
    }
}
